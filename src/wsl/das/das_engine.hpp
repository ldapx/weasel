#pragma once

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#endif
#ifndef IN_MODULE_INTERFACE
#include <memory>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <filesystem>
#endif
#ifndef IN_MODULE_INTERFACE
#include <vector>
#endif
#ifndef IN_MODULE_INTERFACE
#include <unordered_map>
#endif
#ifndef IN_MODULE_INTERFACE
#include <csetjmp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <functional>
#endif

namespace das
{
struct StructInfo;
class Context;
}

namespace wsl::das
{

/**
 * In-process AOT compilation of a .das file.
 *
 * Unlike the standalone `daslang -aot` (which resolves `require weasel_api`
 * to a hand-written stub and emits empty proxy placeholders), this compiles
 * the script with the engine's real module environment (Module_WeaselApi,
 * Module_Ecs) loaded. The generated C++ therefore references the real proxy
 * types and includes the module's aotRequire headers, so it actually runs as
 * AOT rather than falling back to the interpreter.
 *
 * :param input:  Path to the .das file to compile.
 * :param output: Path to write the generated .das.cpp to.
 * :param error:  Output error message on failure.
 * :return: \c true on success, \c false on failure (see error).
 */
bool aot_compile_file (const std::string &input, const std::string &output,
                       std::string &error,
                       const std::vector<std::string> &module_roots = {});

/**
 * Structured error from a daScript runtime panic.
 *
 * Captured after evalWithCatch when getException() is non-null.
 * Provides the panic message and source location for diagnostics.
 */
struct das_error
{
  std::string message;
  std::string file;
  int line = 0;
  int column = 0;

  bool
  has_error () const
  {
    return !message.empty ();
  }
};

/**
 * daslang virtual machine wrapper.
 *
 * Provides an interface for compiling and executing daslang files,
 * as well as finding and calling daslang functions.
 */
class das_engine
{
public:
  das_engine ();
  ~das_engine ();

  // Non-copyable
  das_engine (const das_engine &) = delete;
  das_engine &operator= (const das_engine &) = delete;

  /**
   * One-time global daScript initialization on the main thread.
   *
   * Calls Module::Initialize() exactly once. Registered an atexit handler
   * for Module::Shutdown(). Must be called from the main thread before any
   * worker thread creates a das_engine.
   */
  static bool initialize_global ();

  /**
   * Initializes a das engine instance (compilation infrastructure).
   * :return: \c true on success, \c false on failure.
   */
  bool initialize ();

  /**
   * Adds a filesystem root for module resolution.
   *
   * When daScript encounters a bare `require name`, it resolves the module
   * by looking for `name.das` in registered extra roots.  This allows
   * cross-directory requires (e.g. a system file requiring a component
   * from a different directory) to work in interpret mode.
   *
   * :param name: Short name used as the root key (e.g. "components").
   * :param path: Absolute path to the directory containing .das files.
   */
  void addFsRoot (const std::string &name, const std::string &path);

  /**
   * Compiles and executes a .das file.
   * :param path: Path to the daslang file.
   * :return: \c true on success, \c false on failure.
   */
  bool execute_file (const std::filesystem::path &path);

  /** Storage class of a struct field, as far as component storage cares. */
  enum class field_type_kind
  {
    integer,
    unsigned_integer,
    floating,
    boolean,
    /** float2, float3 or float4; size is 8, 12 or 16 bytes. */
    floating_vector,
    /** int2, int3 or int4; size is 8, 12 or 16 bytes. */
    integer_vector,
    /** Managed memory (strings); cannot be stored in component blocks. */
    string,
    unsupported
  };

  struct field_info
  {
    std::string name;
    std::string type_name;
    int offset = 0;
    int size = 0;
    field_type_kind kind = field_type_kind::unsupported;
    std::vector<uint8_t> default_value;
  };

  struct struct_info
  {
    std::vector<field_info> fields;
    int size_of = 0;
  };

  /**
   * Returns the fields and size of a struct defined in the given file.
   *
   * Must be called after execute_file for the same path.
   */
  struct_info get_struct_info (const std::filesystem::path &path,
                               const std::string &struct_name);

  /**
   * Allocates a zero-initialized instance of a daslang struct on the
   *        VM heap. The pointer remains valid for the lifetime of the engine.
   *
   * Must be called after execute_file for the same path.
   */
  void *allocate_instance (const std::filesystem::path &path,
                           const std::string &struct_name);

  /** Returns a pointer to a field within a VM-allocated instance. */
  static void *get_field_ptr (void *instance, int offset);

  /**
   * Copies `field_size` bytes from a field within an instance
   *        into `out`.
   */
  static void get_field (void *instance, int offset, int field_size, void *out,
                         int out_size);

  /**
   * Copies `field_size` bytes from `value` into a field within
   *        an instance.
   */
  static void set_field (void *instance, int offset, int field_size,
                         const void *value, int value_size);

  /**
   * Finds and calls a void function by name with no arguments.
   * :param func_name: Name of the function to call.
   * :return: \c true if the function was found and called, \c false otherwise.
   */
  bool call_void_function (const char *func_name);

  /**
   * Finds and calls a void function from a specific file by name.
   *
   * Uses per-file function lookup so different systems with identically
   * named functions (e.g. "on_update") each resolve to their own file's
   * function.
   *
   * :param path: Path to the compiled .das file.
   * :param func_name: Name of the function to call.
   * :return: \c true if the function was found and called, \c false otherwise.
   */
  bool call_void_function (const std::filesystem::path &path,
                           const char *func_name);

  /**
   * Finds and calls a function returning int by name with no arguments.
   * :param func_name: Name of the function to call.
   * :param out_value: Pointer to store the return value (may be nullptr).
   * :return: \c true if the function was found and called, \c false otherwise.
   */
  bool call_int_function (const char *func_name, int *out_value = nullptr);

  /** Information about an instantiated daslang class. */
  struct class_instance
  {
    void *ptr = nullptr; /// Pointer to the class instance (VM heap)
    const ::das::StructInfo *info = nullptr; /// StructInfo for adapter offsets
    ::das::Context *ctx = nullptr; /// Execution context owning the instance
  };

  /**
   * Instantiates a daslang class defined in the given file.
   *
   * Finds the class by name, allocates an instance on the VM heap,
   * and returns the instance info needed for the class adapter pattern.
   *
   * Must be called after execute_file for the same path.
   *
   * :param path: Path to the compiled .das file.
   * :param class_name: Name of the class to instantiate.
   * :param error: Output error message on failure.
   * :return: The class instance info, or a zeroed struct on failure.
   */
  class_instance instantiate_class (const std::filesystem::path &path,
                                    const std::string &class_name,
                                    std::string &error);

  /**
   * Checks if a class with the given name exists in the compiled program.
   *
   * Must be called after execute_file for the same path.
   *
   * :param path: Path to the compiled .das file.
   * :param class_name: Name of the class to look for.
   * :return: \c true if the class exists, \c false otherwise.
   */
  bool has_class (const std::filesystem::path &path,
                  const std::string &class_name);

  /** Returns the error message from the last failed operation. */
  const std::string &
  last_error () const
  {
    return m_last_error;
  }

  /** Returns the structured error from the last daScript runtime panic. */
  const das_error &
  last_das_error () const
  {
    return m_last_das_error;
  }

  /**
   * Executes a void function with SIGSEGV protection.
   *
   * Wraps evalWithCatch with a signal handler that converts C-level
   * crashes (null dereference, out-of-bounds) into recoverable errors
   * instead of crashing the engine.
   *
   * :param path: Path to the compiled .das file.
   * :param func_name: Name of the function to call.
   * :return: \c true on success, \c false on panic (check last_error()).
   */
  bool call_void_function_safe (const std::filesystem::path &path,
                                const char *func_name);

  /**
   * Executes a void function by name with daScript panic handling.
   *
   * :param func_name: Name of the function to call (global lookup).
   * :return: \c true on success, \c false on error (check last_error()).
   */
  bool call_void_function_safe (const char *func_name);

  /** Returns the list of executed file paths. */
  const std::vector<std::string> &
  executed_files () const
  {
    return m_executed_files;
  }

  /** Shuts down the daslang VM. */
  void shutdown ();

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
  std::string m_last_error;
  das_error m_last_das_error;
  std::vector<std::string> m_executed_files;
  bool m_initialized = false;
};

/** Stable structural signature for a daScript struct layout; zero if unknown. */
std::uint64_t das_struct_layout_hash (const ::das::StructInfo *type);

} // namespace wsl::das

namespace wsl::das
{

/**
 * Runs a call into daScript and turns a script panic into an error message.
 *
 * Saves and restores the VM's call state, installs the panic target and the
 * per-thread script bookkeeping, and restores the query nesting depth when a
 * panic unwinds through the call. Native faults are not handled here; see
 * das_signal.
 *
 * Must not be called from inside a scope that owns RAII state which has to
 * survive a panic: destructors of frames between the call and the panic do not
 * run. Create such scopes around the call instead.
 *
 * :param ctx:   Context the call runs in.
 * :param label: Name for crash reports, valid for the duration of the call.
 * :param call:  The call to run.
 * :param error: Receives the panic message with its file and line on failure.
 * :return: \c true when the call completed, \c false when it panicked.
 */
bool run_protected_call (::das::Context *ctx, const char *label,
                         const std::function<void ()> &call, das_error &error);

} // namespace wsl::das

namespace wsl::das::das_signal
{
/** Installs the process-wide SIGSEGV policy once and this thread's alt stack.
 */
void install ();
/** Makes sure this thread has the alternate signal stack. */
void ensure_sigstack ();
/** Marks entry into and exit from a script call on this thread. */
const char *enter_script (const char *label) noexcept;
void leave_script (const char *previous_label) noexcept;
} // namespace wsl::das::das_signal
