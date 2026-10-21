#if WEASEL_HAS_DASLANG
#include "wsl_event_binds.hpp"
#include "wsl/das/wsl_api_module.hpp"
#include "wsl/event/message_bus.hpp"
#include "wsl/input.hpp"
#include "wsl/comp/component_meta.hpp"
#include "wsl/log/log.hpp"

#include "daScript/ast/ast.h"
#include "daScript/ast/ast_handle.h"
#include "daScript/ast/ast_interop.h"
#include "daScript/daScriptModule.h"
#include "daScript/misc/arraytype.h"
#include "daScript/simulate/aot.h"

#include <entt/core/hashed_string.hpp>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

// ── Proxy value types exposed to Daslang ──
// Scripts read engine input messages through these value structs. Each holds a
// copy of the posted `wsl::input::*` POD and exposes read-only getters so the
// Daslang struct mirrors the field layout without tying scripts to SDL types.

namespace wsl::das
{

struct MouseMotionMsg
{
  wsl::input::mouse_motion v{};
  void
  bind (const wsl::input::mouse_motion &o)
  {
    v = o;
  }
  int
  x ()
  {
    return v.x;
  }
  int
  y ()
  {
    return v.y;
  }
  int
  xrel ()
  {
    return v.xrel;
  }
  int
  yrel ()
  {
    return v.yrel;
  }
};

struct MouseButtonMsg
{
  wsl::input::mouse_button v{};
  void
  bind (const wsl::input::mouse_button &o)
  {
    v = o;
  }
  int
  button ()
  {
    return static_cast<int> (v.button);
  }
  bool
  down ()
  {
    return v.down;
  }
  int
  x ()
  {
    return v.x;
  }
  int
  y ()
  {
    return v.y;
  }
};

struct MouseWheelMsg
{
  wsl::input::mouse_wheel v{};
  void
  bind (const wsl::input::mouse_wheel &o)
  {
    v = o;
  }
  int
  x ()
  {
    return v.x;
  }
  int
  y ()
  {
    return v.y;
  }
  bool
  flipped ()
  {
    return v.flipped;
  }
};

struct KeyPressedMsg
{
  wsl::input::key_pressed v{};
  void
  bind (const wsl::input::key_pressed &o)
  {
    v = o;
  }
  uint32_t
  scancode ()
  {
    return static_cast<uint32_t> (v.scancode);
  }
  int32_t
  keycode ()
  {
    return static_cast<int32_t> (v.keycode);
  }
  uint32_t
  mod ()
  {
    return static_cast<uint32_t> (v.mod);
  }
  bool
  repeat ()
  {
    return v.repeat;
  }
};

struct KeyReleasedMsg
{
  wsl::input::key_released v{};
  void
  bind (const wsl::input::key_released &o)
  {
    v = o;
  }
  uint32_t
  scancode ()
  {
    return static_cast<uint32_t> (v.scancode);
  }
  int32_t
  keycode ()
  {
    return static_cast<int32_t> (v.keycode);
  }
  uint32_t
  mod ()
  {
    return static_cast<uint32_t> (v.mod);
  }
};

struct TextInputMsg
{
  wsl::input::text_input v{};
  void
  bind (const wsl::input::text_input &o)
  {
    v = o;
  }
};

struct WindowResizedMsg
{
  wsl::input::window_resized v{};
  void
  bind (const wsl::input::window_resized &o)
  {
    v = o;
  }
  int
  width ()
  {
    return v.width;
  }
  int
  height ()
  {
    return v.height;
  }
};

struct QuitRequestedMsg
{
  wsl::input::quit_requested v{};
  void
  bind (const wsl::input::quit_requested &o)
  {
    v = o;
  }
  bool
  requested ()
  {
    return v.requested;
  }
};

} // namespace wsl::das

MAKE_TYPE_FACTORY (MouseMotion, wsl::das::MouseMotionMsg)
MAKE_TYPE_FACTORY (MouseButton, wsl::das::MouseButtonMsg)
MAKE_TYPE_FACTORY (MouseWheel, wsl::das::MouseWheelMsg)
MAKE_TYPE_FACTORY (KeyPressed, wsl::das::KeyPressedMsg)
MAKE_TYPE_FACTORY (KeyReleased, wsl::das::KeyReleasedMsg)
MAKE_TYPE_FACTORY (TextInput, wsl::das::TextInputMsg)
MAKE_TYPE_FACTORY (WindowResized, wsl::das::WindowResizedMsg)
MAKE_TYPE_FACTORY (QuitRequested, wsl::das::QuitRequestedMsg)

namespace wsl::das
{

// ── Name-keyed message registry ──

namespace
{

struct message_type_record
{
  entt::id_type id = 0;
  std::size_t size = 0;
};

std::unordered_map<std::string, message_type_record> &
message_type_registry ()
{
  static std::unordered_map<std::string, message_type_record> registry;
  return registry;
}

const message_type_record *
find_message_type (const char *name)
{
  if (name == nullptr) {
    return nullptr;
  }

  auto &registry = message_type_registry ();
  const auto it = registry.find (name);
  return it == registry.end () ? nullptr : &it->second;
}

// ── Name-keyed message API (post / count / for_each) ──

/**
 * Posts a message payload from a daScript struct. Unknown names are
 * registered on first use with a name-hashed id and the posted struct's
 * size, which makes purely script-defined message types work without any
 * C++ counterpart.
 */
vec4f
wsl_message_post (::das::Context &, ::das::SimNode_CallBase *call, vec4f *args)
{
  using namespace ::das;

  const char *name = cast<char *>::to (args[0]);
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (name == nullptr || bus == nullptr) {
    return v_zero ();
  }

  TypeInfo *ti = call->types[1];
  if (ti == nullptr || ti->type != Type::tStructure) {
    wsl::log::sys ()->error (
        "message_post: payload for '{}' must be a structure type", name);
    return v_zero ();
  }

  const void *data = cast<void *>::to (args[1]);
  const std::size_t size = static_cast<std::size_t> (getTypeSize (ti));

  auto &registry = message_type_registry ();
  entt::id_type id = 0;

  const auto it = registry.find (name);
  if (it != registry.end ()) {
    if (it->second.size != 0 && it->second.size != size) {
      wsl::log::sys ()->error (
          "message_post: payload size mismatch for '{}' (expected {}, got {})",
          name, it->second.size, size);
      return v_zero ();
    }
    id = it->second.id;
  } else {
    // Script-defined message type: latch id and layout on first post.
    id = entt::id_type{ entt::hashed_string (name) };
    registry.emplace (name, message_type_record{ id, size });
  }

  if (!bus->post_erased (id, data, size)) {
    wsl::log::sys ()->error ("message_post: rejected post for '{}'", name);
  }

  return v_zero ();
}

int
wsl_message_count (const char *name)
{
  auto *record = find_message_type (name);
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (record == nullptr || bus == nullptr) {
    return 0;
  }

  auto *buf = bus->find_published (record->id);
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return 0;
  }

  return static_cast<int> (buf->bytes.size () / buf->element_size);
}
void
wsl_message_for_each (const char *name,
                      const ::das::TBlock<void, uint32_t> &blk,
                      ::das::Context *context, ::das::LineInfoArg *at)
{
  auto *record = find_message_type (name);
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (record == nullptr || bus == nullptr || context == nullptr) {
    return;
  }

  auto *buf = bus->find_published (record->id);
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return;
  }

  // Drives the block with the element index, mirroring
  // each_world_entity_with: the payload itself is read through
  // message_at (a value copy), so the block never aliases the
  // published buffer that the next drain recycles.
  const std::size_t count = buf->bytes.size () / buf->element_size;
  for (std::size_t i = 0; i < count; ++i) {
    ::das::das_invoke<void>::invoke<uint32_t> (context, at, blk,
                                               static_cast<uint32_t> (i));
  }
}

/**
 * Returns a pointer to the published bytes of message `index`, or nullptr
 * when the type is unknown, unpublished, or the index is out of range.
 * The daslang `message_at` wrapper copies the bytes out, so callers never
 * hold the recycled buffer.
 */
void *
wsl_message_at_data (const char *name, uint32_t index)
{
  auto *record = find_message_type (name);
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (record == nullptr || bus == nullptr) {
    return nullptr;
  }
  auto *buf = bus->find_published (record->id);
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return nullptr;
  }
  const std::size_t count = buf->bytes.size () / buf->element_size;
  if (static_cast<std::size_t> (index) >= count) {
    return nullptr;
  }
  return buf->bytes.data ()
         + static_cast<std::size_t> (index) * buf->element_size;
}

// ── Script message subscriptions ──

struct das_subscription
{
  entt::id_type id = 0;
  std::string name;
  ::das::Func fn;
  ::das::Context *ctx = nullptr;
  // The bus owns its callback forever; this token lets a removed
  // subscription neuter it instead of leaving a dangling context pointer.
  std::shared_ptr<void> token;
};

std::vector<das_subscription> &
subscription_registry ()
{
  static std::vector<das_subscription> subscriptions;
  return subscriptions;
}

bool
wsl_message_subscribe (const char *name, ::das::Func fn,
                       ::das::Context *context)
{
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (name == nullptr || bus == nullptr || context == nullptr
      || fn.PTR == nullptr) {
    return false;
  }

  const message_type_record *record = find_message_type (name);
  entt::id_type id = 0;
  if (record != nullptr) {
    id = record->id;
  } else {
    // Subscribing to an unknown name is allowed: the id latches so posts
    // from the same name resolve to the same subscription channel.
    id = entt::id_type{ entt::hashed_string (name) };
    register_message_type (name, id, 0);
  }

  auto token = std::make_shared<int> (0);

  das_subscription sub{ id, name, fn, context, token };
  subscription_registry ().push_back (std::move (sub));

  const std::weak_ptr<void> weak_token = token;
  entt::id_type captured_id = id;
  bus->subscribe (
      [captured_id, weak_token, fn, context] (const wsl::event::message &m) {
        if (weak_token.expired () || m.type_id != captured_id
            || m.data == nullptr) {
          return;
        }
        ::das::das_invoke_function<void>::invoke<void *> (
            context, nullptr, fn, const_cast<void *> (m.data));
      });

  return true;
}

bool
wsl_message_unsubscribe (const char *name, ::das::Context *context)
{
  if (name == nullptr || context == nullptr) {
    return false;
  }

  auto &subs = subscription_registry ();
  std::erase_if (subs, [name, context] (const das_subscription &s) {
    return s.name == name && s.ctx == context;
  });
  return true;
}

} // namespace

void
wsl_api_on_context_destroyed (::das::Context *context)
{
  if (context == nullptr) {
    return;
  }

  auto &subs = subscription_registry ();
  std::erase_if (
      subs, [context] (const das_subscription &s) { return s.ctx == context; });
}

void
register_message_type (const char *name, entt::id_type id, std::size_t size)
{
  if (name == nullptr) {
    return;
  }
  message_type_registry ().insert_or_assign (name,
                                             message_type_record{ id, size });
}

template <typename Pod>
size_t
msg_count ()
{
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (bus == nullptr) {
    return 0;
  }
  auto *buf = bus->find_published (wsl::comp::stable_type_id<Pod> ());
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return 0;
  }
  return buf->bytes.size () / buf->element_size;
}

template <typename Pod, typename Proxy>
Proxy
msg_at (size_t idx)
{
  Proxy p{};
  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (bus == nullptr) {
    return p;
  }
  auto *buf = bus->find_published (wsl::comp::stable_type_id<Pod> ());
  if (buf == nullptr || buf->bytes.empty () || buf->element_size == 0) {
    return p;
  }
  const size_t n = buf->bytes.size () / buf->element_size;
  if (idx >= n) {
    return p;
  }
  Pod v{};
  std::memcpy (&v, buf->bytes.data () + idx * buf->element_size,
               buf->element_size);
  p.bind (v);
  return p;
}

int
wsl_mouse_motion_count ()
{
  return static_cast<int> (msg_count<wsl::input::mouse_motion> ());
}
wsl::das::MouseMotionMsg
wsl_mouse_motion_at (int idx)
{
  return msg_at<wsl::input::mouse_motion, wsl::das::MouseMotionMsg> (
      static_cast<size_t> (idx));
}

int
wsl_mouse_button_count ()
{
  return static_cast<int> (msg_count<wsl::input::mouse_button> ());
}
wsl::das::MouseButtonMsg
wsl_mouse_button_at (int idx)
{
  return msg_at<wsl::input::mouse_button, wsl::das::MouseButtonMsg> (
      static_cast<size_t> (idx));
}

int
wsl_mouse_wheel_count ()
{
  return static_cast<int> (msg_count<wsl::input::mouse_wheel> ());
}
wsl::das::MouseWheelMsg
wsl_mouse_wheel_at (int idx)
{
  return msg_at<wsl::input::mouse_wheel, wsl::das::MouseWheelMsg> (
      static_cast<size_t> (idx));
}

int
wsl_key_pressed_count ()
{
  return static_cast<int> (msg_count<wsl::input::key_pressed> ());
}
wsl::das::KeyPressedMsg
wsl_key_pressed_at (int idx)
{
  return msg_at<wsl::input::key_pressed, wsl::das::KeyPressedMsg> (
      static_cast<size_t> (idx));
}

int
wsl_key_released_count ()
{
  return static_cast<int> (msg_count<wsl::input::key_released> ());
}
wsl::das::KeyReleasedMsg
wsl_key_released_at (int idx)
{
  return msg_at<wsl::input::key_released, wsl::das::KeyReleasedMsg> (
      static_cast<size_t> (idx));
}

int
wsl_text_input_count ()
{
  return static_cast<int> (msg_count<wsl::input::text_input> ());
}
wsl::das::TextInputMsg
wsl_text_input_at (int idx)
{
  return msg_at<wsl::input::text_input, wsl::das::TextInputMsg> (
      static_cast<size_t> (idx));
}

const char *
wsl_text_input_text (int idx)
{
  static thread_local std::string s_text;
  s_text.clear ();

  auto *bus = wsl::das::wsl_api_get_active_message_bus ();
  if (bus != nullptr) {
    auto *buf = bus->find_published (
        wsl::comp::stable_type_id<wsl::input::text_input> ());
    if (buf != nullptr && !buf->bytes.empty () && buf->element_size > 0) {
      const size_t n = buf->bytes.size () / buf->element_size;
      if (static_cast<size_t> (idx) < n) {
        wsl::input::text_input v{};
        std::memcpy (&v,
                     buf->bytes.data ()
                         + static_cast<size_t> (idx) * buf->element_size,
                     buf->element_size);
        s_text = v.text;
      }
    }
  }
  return s_text.c_str ();
}

int
wsl_window_resized_count ()
{
  return static_cast<int> (msg_count<wsl::input::window_resized> ());
}
wsl::das::WindowResizedMsg
wsl_window_resized_at (int idx)
{
  return msg_at<wsl::input::window_resized, wsl::das::WindowResizedMsg> (
      static_cast<size_t> (idx));
}

int
wsl_quit_requested_count ()
{
  return static_cast<int> (msg_count<wsl::input::quit_requested> ());
}
wsl::das::QuitRequestedMsg
wsl_quit_requested_at (int idx)
{
  return msg_at<wsl::input::quit_requested, wsl::das::QuitRequestedMsg> (
      static_cast<size_t> (idx));
}

// ── Annotations ──

struct MouseMotionMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::MouseMotionMsg, false, false>
{
  explicit MouseMotionMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::MouseMotionMsg, false,
                                          false> ("MouseMotion", lib,
                                                  "wsl::das::MouseMotionMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (x)> ("x", "x");
    addProperty<DAS_BIND_MANAGED_PROP (y)> ("y", "y");
    addProperty<DAS_BIND_MANAGED_PROP (xrel)> ("xrel", "xrel");
    addProperty<DAS_BIND_MANAGED_PROP (yrel)> ("yrel", "yrel");
  }
};

struct MouseButtonMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::MouseButtonMsg, false, false>
{
  explicit MouseButtonMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::MouseButtonMsg, false,
                                          false> ("MouseButton", lib,
                                                  "wsl::das::MouseButtonMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (button)> ("button", "button");
    addProperty<DAS_BIND_MANAGED_PROP (down)> ("down", "down");
    addProperty<DAS_BIND_MANAGED_PROP (x)> ("x", "x");
    addProperty<DAS_BIND_MANAGED_PROP (y)> ("y", "y");
  }
};

struct MouseWheelMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::MouseWheelMsg, false, false>
{
  explicit MouseWheelMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::MouseWheelMsg, false,
                                          false> ("MouseWheel", lib,
                                                  "wsl::das::MouseWheelMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (x)> ("x", "x");
    addProperty<DAS_BIND_MANAGED_PROP (y)> ("y", "y");
    addProperty<DAS_BIND_MANAGED_PROP (flipped)> ("flipped", "flipped");
  }
};

struct KeyPressedMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::KeyPressedMsg, false, false>
{
  explicit KeyPressedMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::KeyPressedMsg, false,
                                          false> ("KeyPressed", lib,
                                                  "wsl::das::KeyPressedMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (scancode)> ("scancode", "scancode");
    addProperty<DAS_BIND_MANAGED_PROP (keycode)> ("keycode", "keycode");
    addProperty<DAS_BIND_MANAGED_PROP (mod)> ("mod", "mod");
    addProperty<DAS_BIND_MANAGED_PROP (repeat)> ("repeat", "repeat");
  }
};

struct KeyReleasedMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::KeyReleasedMsg, false, false>
{
  explicit KeyReleasedMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::KeyReleasedMsg, false,
                                          false> ("KeyReleased", lib,
                                                  "wsl::das::KeyReleasedMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (scancode)> ("scancode", "scancode");
    addProperty<DAS_BIND_MANAGED_PROP (keycode)> ("keycode", "keycode");
    addProperty<DAS_BIND_MANAGED_PROP (mod)> ("mod", "mod");
  }
};

struct TextInputMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::TextInputMsg, false, false>
{
  explicit TextInputMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::TextInputMsg, false,
                                          false> ("TextInput", lib,
                                                  "wsl::das::TextInputMsg")
  {
  }
};

struct WindowResizedMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::WindowResizedMsg, false,
                                        false>
{
  explicit WindowResizedMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::WindowResizedMsg, false,
                                          false> ("WindowResized", lib,
                                                  "wsl::das::WindowResizedMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (width)> ("width", "width");
    addProperty<DAS_BIND_MANAGED_PROP (height)> ("height", "height");
  }
};

struct QuitRequestedMsgAnnotation
    : ::das::ManagedStructureAnnotation<wsl::das::QuitRequestedMsg, false,
                                        false>
{
  explicit QuitRequestedMsgAnnotation (::das::ModuleLibrary &lib)
      : ::das::ManagedStructureAnnotation<wsl::das::QuitRequestedMsg, false,
                                          false> ("QuitRequested", lib,
                                                  "wsl::das::QuitRequestedMsg")
  {
    addProperty<DAS_BIND_MANAGED_PROP (requested)> ("requested", "requested");
  }
};

} // namespace

namespace wsl::das
{

void
register_event_message_bindings (::das::Module &module,
                                 ::das::ModuleLibrary &lib)
{
  // Register the built-in input messages so name-keyed APIs can address
  // them (post/count/for_each) with engine-consistent type ids.
  register_message_type ("mouse_motion",
                         wsl::comp::stable_type_id<wsl::input::mouse_motion> (),
                         sizeof (wsl::input::mouse_motion));
  register_message_type ("mouse_button",
                         wsl::comp::stable_type_id<wsl::input::mouse_button> (),
                         sizeof (wsl::input::mouse_button));
  register_message_type ("mouse_wheel",
                         wsl::comp::stable_type_id<wsl::input::mouse_wheel> (),
                         sizeof (wsl::input::mouse_wheel));
  register_message_type ("key_pressed",
                         wsl::comp::stable_type_id<wsl::input::key_pressed> (),
                         sizeof (wsl::input::key_pressed));
  register_message_type ("key_released",
                         wsl::comp::stable_type_id<wsl::input::key_released> (),
                         sizeof (wsl::input::key_released));
  register_message_type ("text_input",
                         wsl::comp::stable_type_id<wsl::input::text_input> (),
                         sizeof (wsl::input::text_input));
  register_message_type (
      "window_resized",
      wsl::comp::stable_type_id<wsl::input::window_resized> (),
      sizeof (wsl::input::window_resized));
  register_message_type (
      "quit_requested",
      wsl::comp::stable_type_id<wsl::input::quit_requested> (),
      sizeof (wsl::input::quit_requested));

  module.addAnnotation (new MouseMotionMsgAnnotation (lib));
  module.addAnnotation (new MouseButtonMsgAnnotation (lib));
  module.addAnnotation (new MouseWheelMsgAnnotation (lib));
  module.addAnnotation (new KeyPressedMsgAnnotation (lib));
  module.addAnnotation (new KeyReleasedMsgAnnotation (lib));
  module.addAnnotation (new TextInputMsgAnnotation (lib));
  module.addAnnotation (new WindowResizedMsgAnnotation (lib));
  module.addAnnotation (new QuitRequestedMsgAnnotation (lib));

  addExtern<DAS_BIND_FUN (wsl_mouse_motion_count)> (
      module, lib, "mouse_motion_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_motion_count");
  addExtern<DAS_BIND_FUN (wsl_mouse_motion_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "mouse_motion_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_motion_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_mouse_button_count)> (
      module, lib, "mouse_button_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_button_count");
  addExtern<DAS_BIND_FUN (wsl_mouse_button_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "mouse_button_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_button_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_mouse_wheel_count)> (
      module, lib, "mouse_wheel_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_wheel_count");
  addExtern<DAS_BIND_FUN (wsl_mouse_wheel_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "mouse_wheel_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_mouse_wheel_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_key_pressed_count)> (
      module, lib, "key_pressed_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_key_pressed_count");
  addExtern<DAS_BIND_FUN (wsl_key_pressed_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "key_pressed_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_key_pressed_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_key_released_count)> (
      module, lib, "key_released_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_key_released_count");
  addExtern<DAS_BIND_FUN (wsl_key_released_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "key_released_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_key_released_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_text_input_count)> (
      module, lib, "text_input_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_text_input_count");
  addExtern<DAS_BIND_FUN (wsl_text_input_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "text_input_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_text_input_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_text_input_text)> (
      module, lib, "text_input_text", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_text_input_text")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_window_resized_count)> (
      module, lib, "window_resized_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_window_resized_count");
  addExtern<DAS_BIND_FUN (wsl_window_resized_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "window_resized_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_window_resized_at")
      ->args ({ "index" });

  addExtern<DAS_BIND_FUN (wsl_quit_requested_count)> (
      module, lib, "quit_requested_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_quit_requested_count");
  addExtern<DAS_BIND_FUN (wsl_quit_requested_at),
            ::das::SimNode_ExtFuncCallAndCopyOrMove> (
      module, lib, "quit_requested_at", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_quit_requested_at")
      ->args ({ "index" });

  // ── Name-keyed message API ──
  addInterop<wsl_message_post, bool, const char *, vec4f> (
      module, lib, "message_post", ::das::SideEffects::modifyExternal,
      "wsl::das::wsl_message_post")
      ->args ({ "name", "payload" });

  addExtern<DAS_BIND_FUN (wsl_message_count)> (
      module, lib, "message_count", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_message_count")
      ->arg ("name");

  addExtern<DAS_BIND_FUN (wsl_message_for_each)> (
      module, lib, "for_each_message", ::das::SideEffects::invoke,
      "wsl::das::wsl_message_for_each")
      ->args ({ "name", "block", "context", "at" });

  addExtern<DAS_BIND_FUN (wsl_message_at_data)> (
      module, lib, "_message_at_data", ::das::SideEffects::accessExternal,
      "wsl::das::wsl_message_at_data")
      ->args ({ "name", "index" });

  addExtern<DAS_BIND_FUN (wsl_message_subscribe)> (
      module, lib, "message_subscribe", ::das::SideEffects::modifyExternal,
      "wsl::das::wsl_message_subscribe")
      ->args ({ "name", "fn", "context" });

  addExtern<DAS_BIND_FUN (wsl_message_unsubscribe)> (
      module, lib, "message_unsubscribe", ::das::SideEffects::modifyExternal,
      "wsl::das::wsl_message_unsubscribe")
      ->args ({ "name", "context" });
}

} // namespace wsl::das
#endif
