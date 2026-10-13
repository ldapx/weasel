#include "das_system_adapter.hpp"
#include "das_engine.hpp"
#if WEASEL_HAS_DASLANG
#include "wsl_api_module.hpp"
#include "../log/log.hpp"
#include "../comp/singl/runtime_context.hpp"

#include "daScript/daScript.h"

namespace wsl::das
{

namespace
{

das_system_adapter *g_current_das_adapter = nullptr;

/** Sets the active adapter for the duration of a script lifecycle call. */
class current_adapter_guard
{
public:
  explicit current_adapter_guard (das_system_adapter *adapter)
      : m_previous (g_current_das_adapter)
  {
    g_current_das_adapter = adapter;
  }

  ~current_adapter_guard () { g_current_das_adapter = m_previous; }

  current_adapter_guard (const current_adapter_guard &) = delete;
  current_adapter_guard &operator= (const current_adapter_guard &) = delete;

private:
  das_system_adapter *m_previous;
};

class active_registry_guard
{
public:
  explicit active_registry_guard (entt::registry &registry)
      : m_previous (wsl_api_set_active_registry (&registry))
  {
  }

  ~active_registry_guard () { wsl_api_set_active_registry (m_previous); }

  active_registry_guard (const active_registry_guard &) = delete;
  active_registry_guard &operator= (const active_registry_guard &) = delete;

private:
  entt::registry *m_previous;
};

template <typename Fn>
bool
safe_invoke (Context *ctx, const char *method_name,
             const std::string &script_path, Fn &&fn)
{
  das_error error;
  if (!run_protected_call (
          ctx, method_name,
          std::function<void ()> (std::forward<Fn> (fn)), error)) {
    wsl::log::xmake ()->error (
        "das_system_adapter::{} failed in '{}': {} (at {}:{})", method_name,
        script_path, error.message, error.file, error.line);
    return false;
  }
  return true;
}

} // anonymous namespace

das_system_adapter::das_system_adapter (const std::string &name,
                                        const std::string &script_path,
                                        das_engine &engine,
                                        entt::id_type type_id, void *class_ptr,
                                        const StructInfo *class_info,
                                        Context *ctx)
    : sys::ecs_system (name), EcsSystemAdapter (class_info),
      m_script_path (script_path), m_engine (engine), m_type_id (type_id),
      m_class_ptr (class_ptr), m_class_info (class_info), m_ctx (ctx)
{
  set_editor_active (false);
}

entt::id_type
das_system_adapter::get_type_id () const
{
  return m_type_id;
}

const char *
das_system_adapter::get_type_name () const
{
  return "das_system";
}

void
das_system_adapter::on_init (entt::registry &registry)
{
  if (!m_class_ptr || !m_ctx) {
    return;
  }
  m_has_failed = false;
  active_registry_guard registry_guard (registry);
  current_adapter_guard adapter_guard (this);
  auto fn = get_on_init (m_class_ptr);
  if (fn) {
    if (!safe_invoke (m_ctx, "on_init", m_script_path,
                      [&] () { invoke_on_init (m_ctx, fn, m_class_ptr); })) {
      m_has_failed = true;
      wsl::log::sys ()->warn ("System '{}' marked as failed, will be skipped",
                              get_name ());
    }
  }
}

void
das_system_adapter::on_update (entt::registry &registry, double dt)
{
  if (!m_class_ptr || !m_ctx) {
    return;
  }
  if (m_has_failed) {
    return;
  }
  active_registry_guard registry_guard (registry);
  current_adapter_guard adapter_guard (this);
  auto fn = get_on_update (m_class_ptr);
  if (fn) {
    if (!safe_invoke (m_ctx, "on_update", m_script_path, [&] () {
          invoke_on_update (m_ctx, fn, m_class_ptr, static_cast<float> (dt));
        })) {
      m_has_failed = true;
      wsl::log::sys ()->warn ("System '{}' marked as failed, will be skipped",
                              get_name ());
    }
  }
}

void
das_system_adapter::on_inactive (entt::registry &registry)
{
  if (!m_class_ptr || !m_ctx) {
    return;
  }
  active_registry_guard registry_guard (registry);
  current_adapter_guard adapter_guard (this);
  if (auto fn = get_on_inactive (m_class_ptr)) {
    if (!safe_invoke (m_ctx, "on_inactive", m_script_path, [&] () {
          invoke_on_inactive (m_ctx, fn, m_class_ptr);
        })) {
      m_has_failed = true;
      wsl::log::sys ()->warn ("System '{}' marked as failed, will be skipped",
                              get_name ());
    }
  }
}

// ── Script-declared event sources / sinks ──

void
das_system_adapter::register_event_sources (event::event_hub &hub)
{
  for (const event_declaration &decl : m_event_sources) {
    hub.declare_event_source_by_id (decl.event_id, decl.event_name,
                                    get_type_id (), get_name (),
                                    decl.event_size);
  }
}

void
das_system_adapter::register_event_sinks (event::event_hub &hub)
{
  // Invokers must stay pointer-stable across replay passes: live connections
  // capture ``owner_ptr`` at connect() time and would dangle otherwise.
  while (m_sink_invokers.size () < m_event_sinks.size ()) {
    auto invoker = std::make_unique<sink_invoker> ();
    invoker->adapter = this;
    invoker->index = m_sink_invokers.size ();
    m_sink_invokers.push_back (std::move (invoker));
  }

  for (std::size_t i = 0; i < m_event_sinks.size (); ++i) {
    const event_declaration &decl = m_event_sinks[i];

    hub.declare_event_sink_by_id (
        decl.event_id, decl.event_name, get_type_id (), get_name (),
        decl.method_name.c_str (), sink_thunk, m_sink_invokers[i].get (),
        decl.event_size, this, decl.payload_layout_hash);
  }
}

bool
das_system_adapter::add_script_event_source (entt::id_type event_id,
                                             std::string_view event_name,
                                             std::size_t event_size)
{
  for (const event_declaration &decl : m_event_sources) {
    if (decl.event_id == event_id) {
      return true;
    }
  }

  m_event_sources.push_back (
      { event_id, std::string (event_name), {}, event_size });

  auto *rc = try_get_runtime_context ();
  if (rc != nullptr) {
    register_event_sources (rc->event_hub ());
  }

  return true;
}

bool
das_system_adapter::add_script_event_sink (entt::id_type event_id,
                                           std::string_view event_name,
                                           std::string_view method_name,
                                           std::size_t event_size,
                                           std::uint64_t payload_layout_hash)
{
  for (const event_declaration &decl : m_event_sinks) {
    if (decl.event_id == event_id && decl.method_name == method_name) {
      return decl.event_size == event_size
             && decl.payload_layout_hash == payload_layout_hash;
    }
  }

  m_event_sinks.push_back (
      { event_id, std::string (event_name), std::string (method_name),
        event_size, payload_layout_hash });

  auto *rc = try_get_runtime_context ();
  if (rc != nullptr) {
    register_event_sinks (rc->event_hub ());
  }

  return true;
}

bool
das_system_adapter::event_handler_payload_signature (
    const char *method_name, std::size_t &payload_size,
    std::uint64_t &layout_hash) const
{
  if (m_class_info == nullptr || m_class_info->fields == nullptr
      || method_name == nullptr) {
    return false;
  }

  for (std::uint32_t i = 0; i < m_class_info->count; ++i) {
    const ::das::VarInfo *method = m_class_info->fields[i];
    if (method == nullptr || method->name == nullptr
        || std::string_view (method->name) != method_name) {
      continue;
    }
    if ((method->type != ::das::Type::tBlock
         && method->type != ::das::Type::tFunction)
        || method->argCount != 1
        || method->argTypes == nullptr || method->argTypes[0] == nullptr) {
      return false;
    }

    const ::das::TypeInfo *payload = method->argTypes[0];
    if (payload->type != ::das::Type::tStructure
        || payload->structType == nullptr || !payload->isRawPod ()
        || payload->size == 0) {
      return false;
    }

    payload_size = payload->size;
    layout_hash = das_struct_layout_hash (payload->structType);
    return layout_hash != 0;
  }
  return false;
}

bool
das_system_adapter::has_method (const char *method_name) const
{
  if (m_class_info == nullptr || method_name == nullptr) {
    return false;
  }

  return ::das::adapt_field_offset (method_name, m_class_info) != 0;
}

das_system_adapter *
das_system_adapter::current ()
{
  return g_current_das_adapter;
}

void
das_system_adapter::sink_thunk (void *owner, entt::registry &registry,
                                const void *payload)
{
  auto *invoker = static_cast<sink_invoker *> (owner);
  if (invoker == nullptr || invoker->adapter == nullptr) {
    return;
  }

  invoker->adapter->invoke_event_handler (invoker->index, registry, payload);
}

void
das_system_adapter::invoke_event_handler (std::size_t index,
                                          entt::registry &registry,
                                          const void *payload)
{
  if (!m_class_ptr || !m_ctx || m_has_failed) {
    return;
  }

  if (index >= m_event_sinks.size ()) {
    return;
  }

  // The handler offset is resolved lazily: the class StructInfo only becomes
  // fully usable once the script program has been simulated.
  const event_declaration &decl = m_event_sinks[index];
  const int offset
      = ::das::adapt_field_offset (decl.method_name.c_str (), m_class_info);
  if (offset == 0) {
    wsl::log::sys ()->error (
        "Event handler '{}' not found on script system '{}'", decl.method_name,
        get_name ());
    m_has_failed = true;
    return;
  }

  auto fn = getDasClassMethod (m_class_ptr, offset);
  if (!fn) {
    return;
  }

  active_registry_guard registry_guard (registry);
  current_adapter_guard adapter_guard (this);

  // Struct payloads pass by reference through the daslang ABI; a raw pointer
  // in the argument slot is exactly what a struct parameter expects.
  if (!safe_invoke (m_ctx, decl.method_name.c_str (), m_script_path, [&] () {
        ::das::das_invoke_function<void>::invoke<void *, void *> (
            m_ctx, nullptr, fn, m_class_ptr, const_cast<void *> (payload));
      })) {
    m_has_failed = true;
    wsl::log::sys ()->warn ("System '{}' marked as failed, will be skipped",
                            get_name ());
  }

}

} // namespace wsl::das
#else
namespace wsl::das
{
das_system_adapter::das_system_adapter (const std::string &name,
                                        const std::string &, das_engine &engine,
                                        entt::id_type type_id, void *class_ptr,
                                        const ::das::StructInfo *class_info,
                                        ::das::Context *ctx)
    : sys::ecs_system (name), m_engine (engine), m_type_id (type_id),
      m_class_ptr (class_ptr), m_class_info (class_info), m_ctx (ctx)
{
}
entt::id_type
das_system_adapter::get_type_id () const
{
  return m_type_id;
}
const char *
das_system_adapter::get_type_name () const
{
  return "das_system";
}
void
das_system_adapter::on_init (entt::registry &)
{
}
void
das_system_adapter::on_update (entt::registry &, double)
{
}
void
das_system_adapter::on_inactive (entt::registry &)
{
}
void
das_system_adapter::register_event_sources (event::event_hub &)
{
}
void
das_system_adapter::register_event_sinks (event::event_hub &)
{
}
bool
das_system_adapter::event_handler_payload_signature (const char *,
                                                     std::size_t &,
                                                     std::uint64_t &) const
{
  return false;
}
bool
das_system_adapter::has_method (const char *) const
{
  return false;
}
das_system_adapter *
das_system_adapter::current ()
{
  return nullptr;
}
void
das_system_adapter::sink_thunk (void *, entt::registry &, const void *)
{
}
void
das_system_adapter::invoke_event_handler (std::size_t, entt::registry &,
                                          const void *)
{
}
} // namespace wsl::das
#endif
