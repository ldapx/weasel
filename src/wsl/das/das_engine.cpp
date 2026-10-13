#include "das_engine.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>
#include <unordered_set>

#if WEASEL_HAS_DASLANG
#include "daScript/ast/dyn_modules.h"
#include "daScript/ast/ast.h"
#include "daScript/daScript.h"
#include "wsl_api_module.hpp"
#include "das_ecs_binds.hpp"
#include "wsl_event_binds.hpp"
#include "../log/log.hpp"

#include <setjmp.h>
#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

namespace wsl::das
{

std::uint64_t
das_struct_layout_hash (const ::das::StructInfo *type)
{
#if WEASEL_HAS_DASLANG
  if (type == nullptr) {
    return 0;
  }

  constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
  constexpr std::uint64_t fnv_prime = 1099511628211ull;
  std::uint64_t hash = fnv_offset;
  std::unordered_set<const ::das::StructInfo *> active;

  const auto hash_bytes = [&] (const void *data, std::size_t size) {
    const auto *bytes = static_cast<const std::uint8_t *> (data);
    for (std::size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= fnv_prime;
    }
  };
  const auto hash_value = [&] (auto value) { hash_bytes (&value, sizeof value); };

  const auto hash_struct = [&] (const auto &self,
                                const ::das::StructInfo *structure) -> bool {
    if (structure == nullptr
        || (structure->count != 0 && structure->fields == nullptr)
        || !active.insert (structure).second) {
      return false;
    }
    hash_value (structure->size);
    hash_value (structure->count);
    for (std::uint32_t i = 0; i < structure->count; ++i) {
      const ::das::VarInfo *field = structure->fields[i];
      if (field == nullptr || field->name == nullptr) {
        active.erase (structure);
        return false;
      }
      const std::size_t name_size = std::strlen (field->name);
      hash_value (name_size);
      hash_bytes (field->name, name_size);
      hash_value (field->offset);
      hash_value (field->size);
      hash_value (static_cast<std::uint32_t> (field->type));
      hash_value (field->dimSize);
      if (field->dimSize != 0) {
        if (field->dim == nullptr) {
          active.erase (structure);
          return false;
        }
        hash_bytes (field->dim,
                    static_cast<std::size_t> (field->dimSize)
                        * sizeof (std::uint32_t));
      }
      if (field->type == ::das::Type::tStructure
          && !self (self, field->structType)) {
        active.erase (structure);
        return false;
      }
      if (field->type == ::das::Type::tEnumeration
          || field->type == ::das::Type::tEnumeration8
          || field->type == ::das::Type::tEnumeration16
          || field->type == ::das::Type::tEnumeration64) {
        if (field->enumType == nullptr) {
          active.erase (structure);
          return false;
        }
        hash_value (field->enumType->flags);
        hash_value (field->enumType->count);
        for (std::uint32_t value_index = 0;
             value_index < field->enumType->count; ++value_index) {
          const ::das::EnumValueInfo *value
              = field->enumType->fields[value_index];
          if (value == nullptr || value->name == nullptr) {
            active.erase (structure);
            return false;
          }
          const std::size_t value_name_size = std::strlen (value->name);
          hash_value (value_name_size);
          hash_bytes (value->name, value_name_size);
          hash_value (value->value);
        }
      }
    }
    active.erase (structure);
    return true;
  };

  if (!hash_struct (hash_struct, type)) {
    return 0;
  }
  return hash == 0 ? 1 : hash;
#else
  (void)type;
  return 0;
#endif
}

} // namespace wsl::das

using namespace das;

DECLARE_ALL_DEFAULT_MODULES;

// Root of the daslang source tree (daslib/ etc.), resolved at runtime:
//   1. WEASEL_DASLANG_ROOT env var (manual override)
//   2. WEASEL_DASLANG_ROOT compile-time define (xmake package install dir),
//      accepted only if it still exists — package hash dirs change on
//      reinstall, which would strand a stale baked-in path
//   3. newest daslang install under the xmake package cache (~/.xmake)
//   4. CMake-era fallback: <build>/_deps/daslang-src
static bool
valid_das_root (const std::string &root)
{
  return !root.empty ()
         && std::filesystem::exists (root + "/daslib/aot_cpp.das");
}

static std::string
das_root_dir ()
{
  if (const char *env = std::getenv ("WEASEL_DASLANG_ROOT");
      env != nullptr && valid_das_root (env))
    return env;
#if defined(WEASEL_DASLANG_ROOT)
  if (valid_das_root (WEASEL_DASLANG_ROOT))
    return std::string (WEASEL_DASLANG_ROOT);
#endif
  {
    std::string base;
    if (const char *pd = std::getenv ("XMAKE_PKGDIR"); pd != nullptr)
      base = std::string (pd) + "/d/daslang";
    else if (const char *home = std::getenv ("HOME"); home != nullptr)
      base = std::string (home) + "/.xmake/packages/d/daslang";
    std::string best;
    std::filesystem::file_time_type best_time{};
    std::error_code ec;
    for (auto const &ver : std::filesystem::directory_iterator (base, ec))
      for (auto const &inst :
           std::filesystem::directory_iterator (ver.path (), ec)) {
        if (!valid_das_root (inst.path ().string ()))
          continue;
        auto t = std::filesystem::last_write_time (
            inst.path () / "daslib" / "aot_cpp.das", ec);
        if (ec)
          continue;
        if (best.empty () || t > best_time) {
          best = inst.path ().string ();
          best_time = t;
        }
      }
    if (valid_das_root (best))
      return best;
  }
  return std::string (WEASEL_BUILD_DIR) + "/_deps/daslang-src";
}

namespace
{

// Custom FsFileAccess that searches project source directories when
// resolving bare `require` names.  Without this, daScript's built-in
// resolution only checks the same directory as the requiring file,
// which breaks cross-directory requires in interpret mode.
class ProjectFsFileAccess : public FsFileAccess
{
public:
  void
  addProjectRoot (const std::string &name, const std::string &path)
  {
    m_project_roots[name] = path;
  }

  ModuleInfo
  getModuleInfo (const string &req, const string &from) const override
  {
    // For bare names (no '.' or '/'), search project directories first.
    if (req.find_first_of ("./") == string::npos) {
      for (const auto &[name, root] : m_project_roots) {
        std::string candidate = root + "/" + req + ".das";
        if (std::filesystem::exists (candidate)) {
          ModuleInfo info;
          info.moduleName = req;
          info.fileName = candidate;
          return info;
        }
      }
    }
    // Fall back to default FsFileAccess resolution.
    return FsFileAccess::getModuleInfo (req, from);
  }

private:
  std::map<std::string, std::string> m_project_roots;
};

// Create the per-engine-instance file access used by compileDaScript().
// This is instance state, NOT thread state: every das_engine that compiles a
// user .das file needs its own non-null FileAccess, even when the calling
// thread already had all builtin modules initialized by
// das_engine::initialize_global().
void
setup_engine_faccess (smart_ptr<FsFileAccess> &faccess)
{
  // Set up the daslang source tree path and file access.
  setDasRoot (das_root_dir ());

  faccess = smart_ptr<FsFileAccess> (new ProjectFsFileAccess);
  faccess->introduceDaslib ();

  // Add the engine's module directory to ProjectFsFileAccess so that
  // `require weasel_helpers` can find weasel_helpers.das in interpreted mode.
  auto engine_modules
      = std::string (WEASEL_SOURCE_DIR) + "/src/wsl/das/modules";
  static_cast<ProjectFsFileAccess *> (faccess.get ())
      ->addProjectRoot ("engine_modules", engine_modules);
}

// Register the Weasel-specific modules (weasel_api / weasel_ecs) into the
// instance's ModuleGroup.  Guarded per thread: Module_WeaselApi and
// Module_Ecs must only ever be constructed once per thread, otherwise daScript
// aborts with "Module already created".
void
register_weasel_modules_once (ModuleGroup &module_group)
{
  static thread_local bool s_weasel_modules_registered = false;
  if (s_weasel_modules_registered) {
    return;
  }

  // Register the Weasel API module (ECS, transforms, scene queries).
  wsl::das::register_wsl_api_module (module_group);

  // Register the Weasel ECS module (engine component types).
  wsl::das::register_ecs_module (module_group);

  s_weasel_modules_registered = true;
}

bool
initialize_modules_for_engine (TextPrinter &tout,
                               smart_ptr<FsFileAccess> &faccess,
                               ModuleGroup &module_group)
{
  // Ensure the thread-local daScriptEnvironment exists.  Module::Module()
  // accesses daScriptEnvironment::getBound()->modules to insert itself into
  // the per-thread module list.  If the environment hasn't been bound on
  // this thread yet, getBound() returns null and the dereference segfaults.
  // This may be called from a worker thread (async reload), so we must
  // ensure the environment here rather than relying on the main thread.
  ::das::daScriptEnvironment::ensure ();

  setup_engine_faccess (faccess);

  // Register all default builtin modules (BuiltIn, Math, Strings, etc.)
  // into this thread's local ModuleKarma.  ModuleLibrary::addBuiltInModule()
  // calls Module::require("$") which searches the thread-local module list,
  // so the "$" module MUST be present before any Module_WeaselApi or
  // Module_Ecs constructor runs (they create a ModuleLibrary with
  // addBuiltInModule).
  PULL_ALL_DEFAULT_MODULES;

  // Register the Weasel API module (ECS, transforms, scene queries) and the
  // Weasel ECS module (engine component types).  Both are guarded per thread
  // so a second das_engine instance on the same thread reuses them.
  register_weasel_modules_once (module_group);

  // NOTE: We intentionally do NOT call require_dynamic_modules here.
  // It compiles .das_module files and stores them in thread-local
  // ModuleKarma.  When the worker thread later compiles user .das files,
  // the compiled modules may be in an inconsistent state across threads,
  // causing corruption (hash set corruption in buildAccessFlags, etc.).
  // Instead, modules like builtin.das are compiled on-demand by
  // compileDaScript on the worker thread.

  return true;
}

// Ensure the calling thread has a valid daScriptEnvironment with modules
// and g_modulesInitialized set.  Called from execute_file so that
// compileDaScript does not crash on worker threads (async reload).
//
// This function creates ALL modules (builtin + weasel) on the calling
// thread's thread-local daScriptEnvironment.  Module::Module() inserts
// into daScriptEnvironment::getBound()->modules, which is thread-local.
// If modules were created on a different thread (e.g. main thread during
// initialize()), they would NOT be visible on this worker thread.
void
ensure_thread_das_environment (TextPrinter &tout,
                               smart_ptr<FsFileAccess> &faccess,
                               ModuleGroup &module_group)
{
  // The guard tests this INSTANCE's `faccess`, never the thread's
  // g_modulesInitialized.  das_engine::initialize_global() runs
  // Module::Initialize() on the main thread, which already sets
  // g_modulesInitialized there.  The CLI calls initialize_global() before
  // compiling user .das sources, so a g_modulesInitialized-based early return
  // left `faccess` null and compileDaScript() dereferenced a null FileAccess
  // inside addExtraDependency() -> FileAccess::getFileInfo() (SIGSEGV, exit
  // code 139).  When the builtin modules are already present on this thread we
  // only build the per-instance file access and the Weasel modules; pulling the
  // builtin modules again aborts with `Module '$' already created`.
  if (faccess) {
    return; // This instance already initialized on this thread.
  }

  auto *env = ::das::daScriptEnvironment::getBound ();
  if (env && env->g_modulesInitialized) {
    // Builtin modules already exist on this thread; only the per-instance
    // state and the Weasel-specific modules are still missing.
    ::das::daScriptEnvironment::ensure ();
    setup_engine_faccess (faccess);
    register_weasel_modules_once (module_group);
    return;
  }

  // Initialize ALL modules on this thread's thread-local environment.
  // This creates the "$", Math, Strings, etc. builtin modules AND the
  // weasel_api/weasel_ecs modules on THIS thread, so they are visible
  // to Module::require() and compileDaScript on this thread.
  initialize_modules_for_engine (tout, faccess, module_group);
  env = ::das::daScriptEnvironment::getBound ();
  env->g_modulesInitialized = true;
}

} // anonymous namespace

// ── Crash policy for native faults inside daScript ──
//
// A daScript panic is an ordinary script error. It unwinds through the VM's own
// throw buffer and is turned into an error message by run_protected_call.
//
// A SIGSEGV raised while a script call is active is different: it means native
// code (usually a binding) touched invalid memory. Continuing after it means
// running on a possibly corrupted heap, so the process reports the faulting
// script and terminates with the default action (core dump where enabled).

namespace wsl::das::das_signal
{

namespace
{

/** Nesting of script calls on this thread. */
thread_local int tls_depth = 0;
/** Label of the innermost script call, for the crash report. */
thread_local const char *tls_label = nullptr;

} // anonymous namespace

#ifndef _WIN32
namespace
{

void
write_text (const char *text) noexcept
{
  const std::size_t length = std::strlen (text);
  [[maybe_unused]] auto written = ::write (STDERR_FILENO, text, length);
}

void
segv_handler (int)
{
  if (tls_depth > 0) {
    write_text ("\nwsl: fatal SIGSEGV while running daScript");
    if (tls_label != nullptr) {
      write_text (": ");
      write_text (tls_label);
    }
    write_text ("\n");
  }
  signal (SIGSEGV, SIG_DFL);
  raise (SIGSEGV);
}

thread_local bool tls_sigstack_ready = false;

} // anonymous namespace

void
ensure_sigstack ()
{
  if (tls_sigstack_ready) {
    return;
  }
  static thread_local std::vector<char> stack;
  stack.resize (SIGSTKSZ);
  stack_t ss{};
  ss.ss_sp = stack.data ();
  ss.ss_size = stack.size ();
  ss.ss_flags = 0;
  sigaltstack (&ss, nullptr);
  tls_sigstack_ready = true;
}

void
install ()
{
  static const bool installed = [] {
    struct sigaction sa{};
    sa.sa_handler = segv_handler;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset (&sa.sa_mask);
    return sigaction (SIGSEGV, &sa, nullptr) == 0;
  }();
  (void)installed;
  ensure_sigstack ();
}

const char *
enter_script (const char *label) noexcept
{
  const char *previous_label = tls_label;
  ++tls_depth;
  tls_label = label;
  return previous_label;
}

void
leave_script (const char *previous_label) noexcept
{
  --tls_depth;
  tls_label = tls_depth == 0 ? nullptr : previous_label;
}
#else
// Windows has no POSIX signal stacks. A native fault inside a script call then
// ends the process through the default unhandled-exception path.
void
ensure_sigstack ()
{
}

void
install ()
{
}

const char *
enter_script (const char *label) noexcept
{
  const char *previous_label = tls_label;
  ++tls_depth;
  tls_label = label;
  return previous_label;
}

void
leave_script (const char *previous_label) noexcept
{
  --tls_depth;
  tls_label = tls_depth == 0 ? nullptr : previous_label;
}
#endif

} // namespace wsl::das::das_signal

namespace wsl::das
{

bool
run_protected_call (::das::Context *ctx, const char *label,
                    const std::function<void ()> &call, das_error &error)
{
  if (ctx == nullptr) {
    error.message = "No daScript context is available";
    return false;
  }

  auto aa = ctx->abiArg;
  auto acm = ctx->abiCMRES;
  auto atba = ctx->abiThisBlockArg;
  char *EP, *SP;
  ctx->stack.watermark (EP, SP);
  auto *saved_throw_buf = ctx->throwBuf;
  const int query_depth = wsl_api_get_query_depth ();

  das_signal::install ();
  const char *previous_label = das_signal::enter_script (label);
  jmp_buf panic_target;
  ctx->throwBuf = &panic_target;

  const bool panicked = setjmp (panic_target) != 0;
  if (!panicked) {
    call ();
  }

  ctx->throwBuf = saved_throw_buf;
  ctx->abiArg = aa;
  ctx->abiCMRES = acm;
  ctx->abiThisBlockArg = atba;
  ctx->stack.pop (EP, SP);
  das_signal::leave_script (previous_label);

  const char *exception = ctx->getException ();
  if (!panicked && exception == nullptr) {
    return true;
  }

  if (exception != nullptr) {
    error.message = exception;
    if (ctx->exceptionAt.fileInfo != nullptr) {
      error.file = ctx->exceptionAt.fileInfo->name;
    }
    error.line = ctx->exceptionAt.line;
    error.column = ctx->exceptionAt.column;
    ctx->clearException ();
  } else {
    error.message = "daScript panic";
  }
  if (panicked) {
    wsl_api_restore_query_depth (query_depth);
  }
  return false;
}

} // namespace wsl::das

// Defined outside namespace wsl::das because PULL_ALL_DEFAULT_MODULES
// expands to code using das::ModuleKarma which fails inside a nested namespace.

static bool s_das_global_initialized = false;

static void
das_global_atexit ()
{
  if (s_das_global_initialized) {
    // resetFusion must stay off.  daScript's fusion engine is a
    // DAS_THREAD_LOCAL unique_ptr that is only created when a program is
    // simulated.  Weasel compiles user .das files on a worker thread (async
    // reload), so the fusion engine exists on THAT thread while the main
    // thread's instance is still null; Module::Shutdown() ->
    // resetFusionEngine() then dereferences that null thread-local and jumps
    // through a garbage function pointer (SIGSEGV, exit code 139, right after
    // "Shutting down runtime context").
    // Module::ShutdownStandalone() is the only PUBLIC entry point that keeps
    // resetFusion off (Module::shutdownInternal() is private).
    ::das::Module::ShutdownStandalone (/*dumpHandleLeaks=*/true);
  }
}

bool
wsl::das::das_engine::initialize_global ()
{
  if (s_das_global_initialized) {
    return true;
  }
  // Register all default built-in modules and initialize the module registry.
  // This MUST happen on the main thread because Module::Shutdown() (called
  // via atexit, which always runs on the main thread) destroys the global
  // module list and thread-local daScriptEnvironment.
  ::das::register_builtin_modules ();
  ::das::Module::Initialize ();
  s_das_global_initialized = true;
  // atexit runs LIFO — our handler runs before daScript's own atexit handler
  // (which checks g_envTotal and calls _Exit(1) if unbalanced).
  std::atexit (das_global_atexit);
  return true;
}

namespace wsl::das
{

bool
aot_compile_file (const std::string &input, const std::string &output,
                  std::string &error,
                  const std::vector<std::string> &module_roots)
{
  if (!das_engine::initialize_global ()) {
    error = "das_engine::initialize_global failed";
    return false;
  }

  // Module creation is thread-local in daslang. initialize_global() already
  // created the builtin modules on the calling (main) thread, so recreating
  // them here would trip the "Module already created" fatal. Run the AOT
  // generation on a fresh worker thread (mirroring execute_file), where
  // ensure_thread_das_environment() sets up the per-thread module
  // environment exactly once.
  std::string emitted;
  std::string local_error;
  bool ok = false;

  std::thread worker ([&] () {
    ::das::TextPrinter tout;
    ::das::smart_ptr<::das::FsFileAccess> faccess;
    ::das::ModuleGroup module_group;
    ensure_thread_das_environment (tout, faccess, module_group);

    // Expose the input file's directory and any caller-supplied module roots
    // so that `require game_module` (a game-specific .das next to the script)
    // resolves during in-process AOT, exactly as the standalone daslang
    // resolved it from the input file's directory.
    if (auto *proj = static_cast<ProjectFsFileAccess *> (faccess.get ())) {
      auto in_dir = std::filesystem::path (input).parent_path ().string ();
      if (!in_dir.empty ())
        proj->addProjectRoot (in_dir, in_dir);
      for (const auto &root : module_roots)
        if (!root.empty ())
          proj->addProjectRoot (root, root);
    }

    // Compile daslib/aot_cpp.das so we can reuse its `run_aot` emitter, which
    // performs setAotHashes + aotCpp + registerAotCpp + the file boilerplate
    // for an already-compiled program. Using it (instead of the file-based
    // `aot()`) lets us feed OUR program, compiled with the real weasel modules.
    ::das::CodeOfPolicies aot_tool_policies;
    aot_tool_policies.version_2_syntax = true;
    aot_tool_policies.aot_module = true;
    ::das::ModuleGroup dummy_group;
    std::string aot_cpp_path = das_root_dir () + "/daslib/aot_cpp.das";
    auto aot_prog = ::das::compileDaScript (
        aot_cpp_path.c_str (), faccess, tout, dummy_group, aot_tool_policies);
    if (!aot_prog || aot_prog->failed ()) {
      local_error = "failed to compile daslib/aot_cpp.das";
      return;
    }
    uint32_t aot_stack = aot_prog->getContextStackSize ();
    if (aot_stack < 1024 * 1024)
      aot_stack = 1024 * 1024;
    auto aot_ctx = std::make_unique<::das::Context> (aot_stack);
    if (!aot_prog->simulate (*aot_ctx, tout)) {
      local_error = "failed to simulate daslib/aot_cpp.das";
      return;
    }
    bool is_unique = false;
    ::das::SimFunction *run_aot_fn
        = aot_ctx->findFunction ("run_aot", is_unique);
    if (!run_aot_fn) {
      local_error = "run_aot not found in daslib/aot_cpp.das";
      return;
    }

    // Compile the user script with the REAL module environment so that
    // `require weasel_api` resolves to Module_WeaselApi (proxy types +
    // accessors) and the program's aotRequire emits the real proxy includes.
    ::das::CodeOfPolicies input_policies;
    input_policies.version_2_syntax = true;
    // Match `daslang -aot` (getPolicies): compile as an AOT module with
    // fail_on_lack_of_aot_export so that struct/class constructor ("make")
    // functions get hash-suffixed C++ names (e.g. `Foo_<hash>` instead of
    // plain `Foo`). Without this, the emitted constructor `Foo` clashes with
    // the generated `class Foo` and fails to compile with
    // "reference to 'Foo' is ambiguous". We deliberately do NOT set
    // `export_all`: that path emits functions with plain (non-hashed) names
    // and also emits `main`, which the AOT-consumer build does not want.
    input_policies.aot_module = true;
    input_policies.fail_on_lack_of_aot_export = true;
    // NOTE: simulate the input with aot=FALSE (matching `daslang -aot`, which
    // compiles/simulates the script normally and only emits AOT in run_aot).
    // Setting aot=true here would force AOT-link verification during simulate
    // and fail every weasel_api/weasel_helpers function that has no in-tree
    // AOT body registered.
    auto input_prog = ::das::compileDaScript (input.c_str (), faccess, tout,
                                              module_group, input_policies);
    if (!input_prog || input_prog->failed ()) {
      local_error = "failed to compile input script: ";
      if (input_prog)
        for (const auto &e : input_prog->errors)
          local_error += std::string (e.what.c_str ()) + "\n";
      return;
    }
    uint32_t in_stack = input_prog->getContextStackSize ();
    if (in_stack < 1024 * 1024)
      in_stack = 1024 * 1024;
    auto in_ctx = std::make_unique<::das::Context> (in_stack);
    if (!input_prog->simulate (*in_ctx, tout)) {
      local_error = "failed to simulate input script: ";
      for (const auto &e : input_prog->errors)
        local_error += std::string (e.what.c_str ()) + " | ";
      return;
    }

    ::das::CodeOfPolicies run_cop;
    run_cop.aot_lib = false;
    run_cop.cross_platform = false;
    // `run_aot(prog : Program?; var pctx : Context?; ...)`. The first
    // argument is a raw pointer; the `var` second argument is a pointer-type
    // parameter passed by reference, i.e. a pointer to the Context* variable
    // (the daslang compiler inserts this address-of automatically — we must
    // do it by hand when calling via evalWithCatch).
    ::das::Context *in_ctx_raw = in_ctx.get ();
    vec4f aot_args[3];
    aot_args[0]
        = cast<char *>::from (reinterpret_cast<char *> (input_prog.get ()));
    aot_args[1] = cast<char *>::from (reinterpret_cast<char *> (&in_ctx_raw));
    aot_args[2] = cast<::das::CodeOfPolicies *>::from (&run_cop);

    // evalWithCatch signature is (fnPtr, args, cmres).  A `string` return is
    // ABI-returned as a char* (no cmres needed), matching daslang's own
    // `daslang -aot` invocation of run_aot via aot().
    vec4f ret = aot_ctx->evalWithCatch (run_aot_fn, aot_args);
    if (const char *ex = aot_ctx->getException ()) {
      local_error = std::string ("run_aot failed: ") + ex;
      aot_ctx->clearException ();
      return;
    }
    const char *out = cast<char *>::to (ret);
    if (!out || !out[0]) {
      local_error = "aot emission produced empty output";
      return;
    }
    emitted = out;
    ok = true;
  });
  worker.join ();

  if (!ok) {
    error = local_error;
    return false;
  }

  std::ofstream ofs (output, std::ios::binary);
  if (!ofs) {
    error = "failed to open output file: " + output;
    return false;
  }
  ofs << emitted;
  return true;
}

struct das_engine::impl
{
  ::das::TextPrinter tout;
  ::das::smart_ptr<::das::FsFileAccess> faccess;
  ::das::ModuleGroup module_group;
  ::das::CodeOfPolicies policies;
  std::unordered_map<std::string, ::das::ProgramPtr> compiled_programs;
  // Per-program context.  Each program gets its own Context because
  // Context::relocateCode relocates ALL sim nodes (including shared module
  // nodes).  If two programs that share modules (via `require`) are
  // simulated into the same Context, the second program's relocateCode
  // tries to copy already-relocated shared nodes, causing the
  // prefix->magic==0xdeadc0de assertion failure in SimNode::copyNode.
  std::unordered_map<std::string, std::unique_ptr<::das::Context>>
      program_contexts;
  struct fn_entry
  {
    ::das::SimFunction *sf = nullptr;
    ::das::Context *ctx = nullptr;
  };
  // Per-file function lookup.  Each compiled file has its own map of
  // function name → (SimFunction*, Context*).  This prevents different
  // systems from overwriting each other's functions (e.g. two files
  // both defining "on_update").
  std::unordered_map<std::string, std::unordered_map<std::string, fn_entry>>
      file_fn_lookup;
  // Legacy flat lookup — kept for backwards compatibility.  Points to the
  // last-registered function with each name.  Only used when callers
  // don't specify a file path.
  std::unordered_map<std::string, fn_entry> fn_lookup;
  // Persistent DebugInfoHelper for making StructInfo after simulate() returns
  // (ctx->thisHelper is cleared to null at the end of simulate).
  ::das::DebugInfoHelper debug_info_helper;

  impl ()
  {
    policies.aot = true;
    policies.aot_module = true;
    policies.fail_on_no_aot = false;
    // Keep all functions in compiled programs so the C++ side can call
    // them at runtime (on_init, on_update, etc.) even if the das code
    // doesn't reference them from within the script.
    policies.export_all = true;
    // compileDaScript asserts that the thread-local daScriptEnvironment
    // has g_modulesInitialized set. Since compileDaScript may be called
    // from worker threads (async reload), skip this check — the modules
    // are globally initialized on the main thread and the ModuleGroup
    // (passed via compileDaScript) has all required modules.
    policies.no_init_check = true;
    // Use v2 syntax for all .das files (class fields, curly braces, etc.)
    policies.version_2_syntax = true;
    // Enable RTTI so call-macro expansions can build `type<...>` / `default<>`
    // nodes (e.g. the `query` macro reconstructs proxy types).
    policies.rtti = true;
  }

  bool
  initialize ()
  {
    // Module creation is deferred to ensure_thread_das_environment() which
    // runs on the worker thread.  Module::Module() inserts into the
    // calling thread's thread-local daScriptEnvironment, so modules created
    // on the main thread are invisible to the worker thread's Module::require.
    // We only do minimal global setup here (non-thread-local state).
    ::das::daScriptEnvironment::ensure ();
    setDasRoot (das_root_dir ());
    return true;
  }

  void
  addFsRoot (const std::string &name, const std::string &path)
  {
    ensure_thread_das_environment (tout, faccess, module_group);
    if (auto *proj = static_cast<ProjectFsFileAccess *> (faccess.get ())) {
      proj->addProjectRoot (name, path);
    }
  }

  bool
  execute_file (const std::string &path, std::string &error)
  {
    // Ensure thread-local daScriptEnvironment is set up on this thread,
    // including the module list needed by compileDaScript and Module::require.
    ensure_thread_das_environment (tout, faccess, module_group);

    auto program = compileDaScript (path.c_str (), faccess, tout, module_group,
                                    policies);
    if (program->failed ()) {
      std::ostringstream oss;
      for (const auto &err : program->errors) {
        oss << reportError (err.at, err.what, err.extra, err.fixme, err.cerr)
            << "\n";
      }
      error = oss.str ();
      return false;
    }

    // Each program gets its OWN Context.  Context::relocateCode relocates
    // ALL sim nodes including shared module nodes.  If two programs that
    // share modules (via `require`) are simulated into the same Context,
    // the second program's relocateCode tries to copy already-relocated
    // shared nodes, causing the prefix->magic==0xdeadc0de assertion
    // failure in SimNode::copyNode.
    uint32_t needed = program->getContextStackSize ();
    uint32_t initial = needed > 1024 * 1024 ? needed : 1024 * 1024;
    auto ctx = std::make_unique<::das::Context> (initial);

    if (!program->simulate (*ctx, tout)) {
      std::ostringstream oss;
      for (const auto &err : program->errors) {
        oss << reportError (err.at, err.what, err.extra, err.fixme, err.cerr)
            << "\n";
      }
      error = oss.str ();
      return false;
    }

    // Merge this program's functions into fn_lookup, each paired with its
    // owning context.
    auto *raw_ctx = ctx.get ();

    // If this path is re-executed, the previous context dies here; drop any
    // message subscriptions bound to it before it goes away.
    if (auto old_it = program_contexts.find (path);
        old_it != program_contexts.end () && old_it->second) {
      wsl::das::wsl_api_on_context_destroyed (old_it->second.get ());
    }

    program_contexts[path] = std::move (ctx);
    compiled_programs[path] = program;

    if (raw_ctx->tabMnLookup) {
      for (auto &kv : *raw_ctx->tabMnLookup) {
        auto *sf = kv.second;
        if (sf && sf->name) {
          file_fn_lookup[path][sf->name] = { sf, raw_ctx };
          fn_lookup[sf->name] = { sf, raw_ctx };
        }
      }
    }

    return true;
  }

  void *
  allocate_instance (const std::string &path, const std::string &struct_name,
                     std::string &error)
  {
    auto ctx_it = program_contexts.find (path);
    if (ctx_it == program_contexts.end ()) {
      error = "No context available for path: " + path;
      return nullptr;
    }
    auto &ctx = ctx_it->second;

    auto it = compiled_programs.find (path);
    if (it == compiled_programs.end ()) {
      error = "Program not found for path: " + path;
      return nullptr;
    }
    auto program = it->second;
    if (!program || !program->thisModule) {
      error = "Invalid program";
      return nullptr;
    }
    auto structs = program->findStructure (struct_name.c_str ());
    if (structs.empty ()) {
      error = "Struct not found: " + struct_name;
      return nullptr;
    }
    auto st = structs[0];
    int bytes = st->getSizeOf ();
    if (bytes <= 0) {
      error = "Struct has zero size: " + struct_name;
      return nullptr;
    }

    // Allocate zero-initialized memory on the VM heap.
    char *ptr = ctx->allocate (static_cast<uint64_t> (bytes), nullptr);
    memset (ptr, 0, static_cast<std::size_t> (bytes));
    return ptr;
  }

  das_engine::class_instance
  instantiate_class (const std::string &path, const std::string &class_name,
                     std::string &error)
  {
    auto ctx_it = program_contexts.find (path);
    if (ctx_it == program_contexts.end ()) {
      error = "No context available for path: " + path;
      return {};
    }
    auto &ctx = ctx_it->second;

    auto it = compiled_programs.find (path);
    if (it == compiled_programs.end ()) {
      error = "Program not found for path: " + path;
      return {};
    }
    auto program = it->second;
    if (!program || !program->thisModule) {
      error = "Invalid program";
      return {};
    }

    // Find the class in the compiled program
    auto structs = program->findStructure (class_name.c_str ());
    if (structs.empty ()) {
      error = "Class not found: " + class_name;
      return {};
    }
    auto st = structs[0];
    int bytes = st->getSizeOf ();
    if (bytes <= 0) {
      error = "Class has zero size: " + class_name;
      return {};
    }

    // Allocate zero-initialized memory on the VM heap.
    char *ptr = ctx->allocate (static_cast<uint64_t> (bytes), nullptr);
    memset (ptr, 0, static_cast<std::size_t> (bytes));

    // Find the generated default constructor.
    // The constructor has the same name as the class (no backtick) and
    // debugInfo->count == 0 (zero explicit arguments — the implicit
    // 'this' pointer is not counted).
    SimFunction *ctor_fn = nullptr;
    int32_t nfunc = ctx->getTotalFunctions ();
    for (int32_t i = 0; i < nfunc; ++i) {
      auto *sf = ctx->getFunction (i);
      if (sf && sf->name && class_name == sf->name) {
        if (sf->debugInfo && sf->debugInfo->count == 0) {
          ctor_fn = sf;
          break;
        }
      }
    }

    if (ctor_fn) {
      // Pass the pre-allocated instance as the cmres (copy-mode result)
      // buffer. The constructor writes __rtti, __finalize, virtual-method
      // function pointers, and field defaults directly into ptr.
      ctx->evalWithCatch (ctor_fn, nullptr, ptr);

      const char *ex = ctx->getException ();
      if (ex) {
        error = "Constructor call failed: ";
        error += ex;
        if (ctx->exceptionAt.fileInfo) {
          error += " at ";
          error += ctx->exceptionAt.fileInfo->name;
          error += ":";
          error += std::to_string (ctx->exceptionAt.line);
        }
        ctx->clearException ();
        return {};
      }

      das_engine::class_instance inst;
      inst.ptr = ptr;
      inst.ctx = ctx.get ();
      inst.info = debug_info_helper.makeStructureDebugInfo (*st);
      return inst;
    }

    // No constructor found — fall back to raw zeroed allocation.
    // The instance has zeroed function pointers; the class adapter
    // will silently skip every method call.
    das_engine::class_instance result;
    result.ptr = ptr;
    result.ctx = ctx.get ();
    result.info = debug_info_helper.makeStructureDebugInfo (*st);
    return result;
  }

  /**
   * Storage class of a field. Scalars and plain vectors are storable; strings
   * and every other managed or aggregate type are classified as such so that
   * the component can be rejected with a clear reason.
   */
  das_engine::field_type_kind
  classify_field_type (const ::das::TypeDecl *td)
  {
    using kind = das_engine::field_type_kind;
    if (!td) {
      return kind::unsupported;
    }
    if (td->isString ()) {
      return kind::string;
    }
    if (td->isArray () || td->isRef () || td->isPointer () || td->isHandle ()) {
      return kind::unsupported;
    }
    if (td->isBool ()) {
      return kind::boolean;
    }
    switch (td->baseType) {
    case ::das::Type::tFloat2:
    case ::das::Type::tFloat3:
    case ::das::Type::tFloat4:
      return kind::floating_vector;
    case ::das::Type::tInt2:
    case ::das::Type::tInt3:
    case ::das::Type::tInt4:
      return kind::integer_vector;
    default:
      break;
    }
    if (td->isSignedInteger ()) {
      return kind::integer;
    }
    if (td->isUnsignedInteger ()) {
      return kind::unsigned_integer;
    }
    if (td->isFloatOrDouble ()) {
      return kind::floating;
    }
    return kind::unsupported;
  }

  bool
  has_program (const std::string &path) const
  {
    return compiled_programs.find (path) != compiled_programs.end ();
  }

  /**
   * Bytes of a fresh instance of a struct, with every field initialised the way
   * the script initialises it: the struct's generated default constructor is
   * run on zeroed memory in the struct's own context. Empty when the struct
   * has no constructor or the constructor fails.
   */
  std::vector<uint8_t>
  build_default_instance (const std::string &path,
                          const std::string &struct_name, int size)
  {
    auto ctx_it = program_contexts.find (path);
    if (size <= 0 || ctx_it == program_contexts.end () || !ctx_it->second) {
      return {};
    }
    auto &ctx = ctx_it->second;

    char *ptr = ctx->allocate (static_cast<uint64_t> (size), nullptr);
    memset (ptr, 0, static_cast<std::size_t> (size));

    SimFunction *ctor_fn = nullptr;
    const int32_t nfunc = ctx->getTotalFunctions ();
    for (int32_t i = 0; i < nfunc; ++i) {
      auto *sf = ctx->getFunction (i);
      if (sf && sf->name && struct_name == sf->name && sf->debugInfo
          && sf->debugInfo->count == 0) {
        ctor_fn = sf;
        break;
      }
    }
    if (ctor_fn == nullptr) {
      return std::vector<uint8_t> (ptr, ptr + size);
    }

    ctx->evalWithCatch (ctor_fn, nullptr, ptr);
    if (ctx->getException () != nullptr) {
      ctx->clearException ();
      return {};
    }
    return std::vector<uint8_t> (ptr, ptr + size);
  }

  das_engine::struct_info
  get_struct_info (const std::string &path, const std::string &struct_name)
  {
    das_engine::struct_info result;
    auto it = compiled_programs.find (path);
    if (it == compiled_programs.end ()) {
      return result;
    }
    auto program = it->second;
    if (!program || !program->thisModule) {
      return result;
    }
    auto structs = program->findStructure (struct_name.c_str ());
    if (structs.empty ()) {
      return result;
    }
    auto st = structs[0];
    result.size_of = st->getSizeOf ();
    const std::vector<uint8_t> defaults
        = build_default_instance (path, struct_name, result.size_of);

    for (auto &field : st->fields) {
      das_engine::field_info fi;
      fi.name = field.name;
      fi.type_name = field.type ? field.type->describe () : "unknown";
      fi.offset = field.offset;
      fi.kind = classify_field_type (field.type);
      fi.size
          = (field.type && fi.kind != das_engine::field_type_kind::unsupported
             && fi.kind != das_engine::field_type_kind::string)
                ? field.type->getSizeOf ()
                : 0;
      const std::size_t offset = fi.offset;
      const std::size_t size = fi.size;
      if (size > 0 && offset <= defaults.size ()
          && size <= defaults.size () - offset) {
        const auto begin = defaults.begin () + fi.offset;
        fi.default_value.assign (begin, begin + fi.size);
      }
      result.fields.push_back (std::move (fi));
    }
    return result;
  }

  bool
  call_void_function (const char *func_name, std::string &error,
                      das_error &das_err)
  {
    auto it = fn_lookup.find (func_name);
    if (it == fn_lookup.end () || !it->second.sf) {
      error = "Function '";
      error += func_name;
      error += "' not found (fn_lookup size="
               + std::to_string (fn_lookup.size ()) + ").";
      if (!fn_lookup.empty ()) {
        error += " Available:";
        for (auto &kv : fn_lookup) {
          error += " '";
          error += kv.first;
          error += "'";
        }
      }
      return false;
    }

    auto &entry = it->second;
    entry.ctx->evalWithCatch (entry.sf, nullptr);

    const char *ex = entry.ctx->getException ();
    if (ex) {
      das_err.message = ex;
      if (entry.ctx->exceptionAt.fileInfo) {
        das_err.file = entry.ctx->exceptionAt.fileInfo->name;
      }
      das_err.line = entry.ctx->exceptionAt.line;
      das_err.column = entry.ctx->exceptionAt.column;
      entry.ctx->clearException ();
      error = das_err.message;
      return false;
    }
    return true;
  }

  bool
  call_void_function (const std::string &path, const char *func_name,
                      std::string &error, das_error &das_err)
  {
    auto file_it = file_fn_lookup.find (path);
    if (file_it == file_fn_lookup.end ()) {
      error = "No compiled functions for path: " + path;
      return false;
    }
    auto it = file_it->second.find (func_name);
    if (it == file_it->second.end () || !it->second.sf) {
      error = "Function '";
      error += func_name;
      error += "' not found in file '";
      error += path;
      error += "'.";
      return false;
    }

    auto &entry = it->second;
    entry.ctx->evalWithCatch (entry.sf, nullptr);

    const char *ex = entry.ctx->getException ();
    if (ex) {
      das_err.message = ex;
      if (entry.ctx->exceptionAt.fileInfo) {
        das_err.file = entry.ctx->exceptionAt.fileInfo->name;
      }
      das_err.line = entry.ctx->exceptionAt.line;
      das_err.column = entry.ctx->exceptionAt.column;
      entry.ctx->clearException ();
      error = das_err.message;
      return false;
    }
    return true;
  }

  bool
  call_int_function (const char *func_name, int &out_value, std::string &error,
                     das_error &das_err)
  {
    auto it = fn_lookup.find (func_name);
    if (it == fn_lookup.end () || !it->second.sf) {
      error = "Function '";
      error += func_name;
      error += "' not found.";
      return false;
    }

    auto &entry = it->second;
    vec4f result = entry.ctx->evalWithCatch (entry.sf, nullptr);

    const char *ex = entry.ctx->getException ();
    if (ex) {
      das_err.message = ex;
      if (entry.ctx->exceptionAt.fileInfo) {
        das_err.file = entry.ctx->exceptionAt.fileInfo->name;
      }
      das_err.line = entry.ctx->exceptionAt.line;
      das_err.column = entry.ctx->exceptionAt.column;
      entry.ctx->clearException ();
      error = das_err.message;
      return false;
    }

    out_value = cast<int32_t>::to (result);
    return true;
  }

  // Use the common panic boundary; native faults are process-fatal.

  bool
  call_void_function_safe (const char *func_name, std::string &error,
                           das_error &das_err)
  {
    auto it = fn_lookup.find (func_name);
    if (it == fn_lookup.end () || !it->second.sf) {
      error = "Function '";
      error += func_name;
      error += "' not found.";
      return false;
    }

    auto &entry = it->second;
    const bool success = run_protected_call (
        entry.ctx, func_name,
        [ctx = entry.ctx, function = entry.sf] {
          ctx->callWithCopyOnReturn (function, nullptr, nullptr, nullptr);
        },
        das_err);
    if (!success) {
      error = das_err.message;
    }
    return success;
  }

  bool
  call_void_function_safe (const std::string &path, const char *func_name,
                           std::string &error, das_error &das_err)
  {
    auto file_it = file_fn_lookup.find (path);
    if (file_it == file_fn_lookup.end ()) {
      error = "No compiled functions for path: " + path;
      return false;
    }
    auto it = file_it->second.find (func_name);
    if (it == file_it->second.end () || !it->second.sf) {
      error = "Function '";
      error += func_name;
      error += "' not found in file '";
      error += path;
      error += "'.";
      return false;
    }

    auto &entry = it->second;
    const bool success = run_protected_call (
        entry.ctx, func_name,
        [ctx = entry.ctx, function = entry.sf] {
          ctx->callWithCopyOnReturn (function, nullptr, nullptr, nullptr);
        },
        das_err);
    if (!success) {
      error = das_err.message;
    }
    return success;
  }

  void
  shutdown ()
  {
    // Release engine-owned daScript resources (programs, contexts, file
    // access). Do NOT call Module::Shutdown() here — it was never called
    // per-engine instance. Module::Initialize/Shutdown is managed globally on
    // the main thread via initialize_global().
    for (auto &kv : program_contexts) {
      if (kv.second) {
        wsl::das::wsl_api_on_context_destroyed (kv.second.get ());
      }
    }
    fn_lookup.clear ();
    file_fn_lookup.clear ();
    program_contexts.clear ();
    compiled_programs.clear ();
    faccess.reset ();
  }
};

das_engine::das_engine () : m_impl (std::make_unique<impl> ()) {}

das_engine::~das_engine () { shutdown (); }

bool
das_engine::initialize ()
{
  if (m_initialized) {
    return true;
  }

  if (!m_impl->initialize ()) {
    m_last_error = "Failed to initialize daslang engine";
    return false;
  }

  m_initialized = true;
  return true;
}

void
wsl::das::das_engine::addFsRoot (const std::string &name,
                                 const std::string &path)
{
  m_impl->addFsRoot (name, path);
}

bool
wsl::das::das_engine::execute_file (const std::filesystem::path &path)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  if (!m_impl->execute_file (path.string (), m_last_error)) {
    return false;
  }

  m_executed_files.push_back (path.string ());
  return true;
}

das_engine::struct_info
das_engine::get_struct_info (const std::filesystem::path &path,
                             const std::string &struct_name)
{
  if (!m_initialized) {
    return {};
  }
  return m_impl->get_struct_info (path.string (), struct_name);
}

void *
das_engine::allocate_instance (const std::filesystem::path &path,
                               const std::string &struct_name)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return nullptr;
  }
  void *ptr
      = m_impl->allocate_instance (path.string (), struct_name, m_last_error);
  return ptr;
}

void *
das_engine::get_field_ptr (void *instance, int offset)
{
  if (!instance) {
    return nullptr;
  }
  return static_cast<char *> (instance) + offset;
}

void
das_engine::get_field (void *instance, int offset, int field_size, void *out,
                       int out_size)
{
  if (!instance || field_size <= 0 || !out || out_size <= 0) {
    return;
  }
  int copy_size = field_size < out_size ? field_size : out_size;
  std::memcpy (out, static_cast<char *> (instance) + offset,
               static_cast<std::size_t> (copy_size));
}

void
das_engine::set_field (void *instance, int offset, int field_size,
                       const void *value, int value_size)
{
  if (!instance || field_size <= 0 || !value || value_size <= 0) {
    return;
  }
  int copy_size = field_size < value_size ? field_size : value_size;
  std::memcpy (static_cast<char *> (instance) + offset, value,
               static_cast<std::size_t> (copy_size));
}

bool
das_engine::call_void_function (const char *func_name)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  if (m_impl->fn_lookup.empty ()) {
    m_last_error = "No functions loaded";
    return false;
  }

  return m_impl->call_void_function (func_name, m_last_error, m_last_das_error);
}

bool
das_engine::call_void_function (const std::filesystem::path &path,
                                const char *func_name)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  return m_impl->call_void_function (path.string (), func_name, m_last_error,
                                     m_last_das_error);
}

bool
das_engine::call_int_function (const char *func_name, int *out_value)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  if (m_impl->fn_lookup.empty ()) {
    m_last_error = "No functions loaded";
    return false;
  }

  int dummy = 0;
  return m_impl->call_int_function (func_name, out_value ? *out_value : dummy,
                                    m_last_error, m_last_das_error);
}

bool
das_engine::call_void_function_safe (const char *func_name)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  if (m_impl->fn_lookup.empty ()) {
    m_last_error = "No functions loaded";
    return false;
  }

  return m_impl->call_void_function_safe (func_name, m_last_error,
                                          m_last_das_error);
}

bool
das_engine::call_void_function_safe (const std::filesystem::path &path,
                                     const char *func_name)
{
  if (!m_initialized) {
    m_last_error = "Engine not initialized";
    return false;
  }

  return m_impl->call_void_function_safe (path.string (), func_name,
                                          m_last_error, m_last_das_error);
}

das_engine::class_instance
das_engine::instantiate_class (const std::filesystem::path &path,
                               const std::string &class_name,
                               std::string &error)
{
  if (!m_initialized) {
    // Lazy init.  A scene loaded from the registration cache can instantiate a
    // user system before anything called initialize() (compile_and_load only
    // does so on the full-compile path), which used to fail with
    // "Engine not initialized" and made every cached scene lose its systems.
    if (!initialize ()) {
      error = m_last_error.empty () ? "Failed to initialize daslang engine"
                                    : m_last_error;
      return {};
    }
  }

  // Lazy compile.  A scene can be loaded straight from the registration cache
  // (load_cached_metadata), in which case no .das file has been executed yet
  // and every instantiate would fail with "No compiled programs available" /
  // "No context available for path".  Compiling just the file we need keeps the
  // metadata-cache fast path usable for scenes that reference user systems.
  if (!m_impl->has_program (path.string ())) {
    if (!m_impl->execute_file (path.string (), error) && error.empty ()) {
      error = "Failed to compile: " + path.string ();
    }
    if (!m_impl->has_program (path.string ())) {
      m_last_error = error;
      return {};
    }
  }

  auto result = m_impl->instantiate_class (path.string (), class_name, error);
  if (!result.ptr) {
    m_last_error = error;
  }
  return result;
}

bool
das_engine::has_class (const std::filesystem::path &path,
                       const std::string &class_name)
{
  if (!m_initialized && !initialize ()) {
    return false;
  }
  if (!m_impl->has_program (path.string ())) {
    std::string error;
    m_impl->execute_file (path.string (), error);
  }
  if (m_impl->program_contexts.empty ()) {
    return false;
  }
  auto it = m_impl->compiled_programs.find (path.string ());
  if (it == m_impl->compiled_programs.end ()) {
    return false;
  }
  auto program = it->second;
  if (!program || !program->thisModule) {
    return false;
  }
  auto structs = program->findStructure (class_name.c_str ());
  return !structs.empty ();
}

void
das_engine::shutdown ()
{
  if (m_initialized) {
    m_impl->shutdown ();
    m_initialized = false;
  }
}

} // namespace wsl::das

#else

namespace wsl::das
{

std::uint64_t
das_struct_layout_hash (const ::das::StructInfo *)
{
  return 0;
}

struct das_engine::impl
{
};

das_engine::das_engine () : m_impl (std::make_unique<impl> ()) {}
das_engine::~das_engine () = default;

bool
das_engine::initialize ()
{
  m_last_error = "daslang support not enabled";
  return false;
}

bool
das_engine::execute_file (const std::filesystem::path &)
{
  m_last_error = "daslang support not enabled";
  return false;
}

bool
das_engine::call_void_function (const char *)
{
  m_last_error = "daslang support not enabled";
  return false;
}

bool
das_engine::call_int_function (const char *, int *)
{
  m_last_error = "daslang support not enabled";
  return false;
}

bool
das_engine::call_void_function_safe (const char *)
{
  m_last_error = "daslang support not enabled";
  return false;
}

bool
das_engine::call_void_function_safe (const std::filesystem::path &,
                                     const char *)
{
  m_last_error = "daslang support not enabled";
  return false;
}

das_engine::struct_info
das_engine::get_struct_info (const std::filesystem::path &, const std::string &)
{
  return {};
}

void *
das_engine::allocate_instance (const std::filesystem::path &,
                               const std::string &)
{
  m_last_error = "daslang support not enabled";
  return nullptr;
}

void *
das_engine::get_field_ptr (void *instance, int offset)
{
  if (!instance) {
    return nullptr;
  }
  return static_cast<char *> (instance) + offset;
}

void
das_engine::get_field (void *instance, int offset, int field_size, void *out,
                       int out_size)
{
  if (!instance || field_size <= 0 || !out || out_size <= 0) {
    return;
  }
  int copy_size = field_size < out_size ? field_size : out_size;
  std::memcpy (out, static_cast<char *> (instance) + offset,
               static_cast<std::size_t> (copy_size));
}

void
das_engine::set_field (void *instance, int offset, int field_size,
                       const void *value, int value_size)
{
  if (!instance || field_size <= 0 || !value || value_size <= 0) {
    return;
  }
  int copy_size = field_size < value_size ? field_size : value_size;
  std::memcpy (static_cast<char *> (instance) + offset, value,
               static_cast<std::size_t> (copy_size));
}

void
das_engine::shutdown ()
{
}

bool
das_engine::initialize_global ()
{
  return false;
}

void
das_engine::addFsRoot (const std::string &, const std::string &)
{
}

das_engine::class_instance
das_engine::instantiate_class (const std::filesystem::path &,
                               const std::string &, std::string &error)
{
  error = "daslang support not enabled";
  return {};
}

bool
das_engine::has_class (const std::filesystem::path &, const std::string &)
{
  return false;
}

bool
das_engine::call_void_function (const std::filesystem::path &, const char *)
{
  m_last_error = "daslang support not enabled";
  return false;
}

} // namespace wsl::das

namespace wsl::das::das_signal
{
void
install ()
{
}

void
ensure_sigstack ()
{
}

const char *
enter_script (const char *label) noexcept
{
  return label;
}

void
leave_script (const char *) noexcept
{
}

} // namespace wsl::das::das_signal

bool
wsl::das::run_protected_call (::das::Context *, const char *,
                             const std::function<void ()> &, das_error &error)
{
  error.message = "daScript support is not enabled";
  return false;
}

bool
wsl::das::aot_compile_file (const std::string &, const std::string &,
                            std::string &error,
                            const std::vector<std::string> &)
{
  error = "daslang support not enabled";
  return false;
}

#endif
