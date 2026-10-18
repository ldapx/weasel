#pragma once

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <cctype>
#endif
#ifndef IN_MODULE_INTERFACE
#include <concepts>
#endif
#ifndef IN_MODULE_INTERFACE
#include <optional>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string>
#endif
#ifndef IN_MODULE_INTERFACE
#include <string_view>
#endif
#ifndef IN_MODULE_INTERFACE
#include <type_traits>
#endif
#ifndef IN_MODULE_INTERFACE
#include <utility>
#endif

/**
 * Root namespace for the Weasel engine.
 */
namespace wsl
{

/**
 * Contains all ECS component definitions and metadata utilities.
 */
namespace comp
{

/**
 * Explicit base type for entity-owned ECS components.
 *
 * World components remain regular value types. This base exists only as a
 * non-owning concept tag so aggregate component construction still works.
 */
struct world_component
{
};

/** Explicit base type for scene/global singleton components. */
struct singleton_component
{
};

// Only allow component types that expose:
//   static void register_meta();
template <typename T>
concept has_register_meta = requires {
  { T::register_meta () } -> std::same_as<void>;
};

/** Concept for types that model entity-owned world components. */
template <typename T>
concept world_component_type = std::derived_from<T, world_component>;

/** Concept for types that model scene/global singleton components. */
template <typename T>
concept singleton_component_type = std::derived_from<T, singleton_component>;

template <typename... Components>
void
register_component_meta ()
{
  (([] {
     if constexpr (has_register_meta<Components>) {
       Components::register_meta ();
     }
   }()),
   ...);
}

template <typename T> struct type_traits
{
  static constexpr std::string_view
  name ()
  {
    return entt::type_name<T>::value ();
  }
};

template <typename T>
inline entt::id_type
stable_type_id ()
{
  using namespace entt::literals;
  std::string name = std::string (type_traits<T>::name ());

  // Strip compiler-specific artifacts (like GCC's trailing ']')
  while (!name.empty ()
         && (name.back () == ']' || name.back () == ' ' || name.back () == '\n'
             || name.back () == '\r')) {
    name.pop_back ();
  }

  return entt::hashed_string::value (name.c_str (), name.size ());
}

struct meta_info
{
  std::string display_name;
  std::string description;
  std::string icon_path;
  /**
   * When true the inspector shows the value as read-only text. Used for
   * derived/computed fields such as resolved viewport resolutions.
   */
  bool read_only = false;
};

/**
 * Returns the unqualified portion of a reflected identifier.
 * :param identifier: Fully qualified identifier or member name.
 * :return: The portion after the last namespace separator.
 */
inline std::string_view
leaf_identifier (std::string_view identifier)
{
  const std::size_t separator = identifier.rfind ("::");
  if (separator == std::string_view::npos) {
    return identifier;
  }

  return identifier.substr (separator + 2);
}

/**
 * Converts a reflected identifier into a human-friendly label.
 * :param identifier: Member name or type name to humanize.
 * :return: Title-cased text suitable for inspector labels.
 */
inline std::string
humanize_identifier (std::string_view identifier)
{
  const std::string_view name = leaf_identifier (identifier);
  std::string out;
  out.reserve (name.size () + 4);

  bool capitalize_next = true;

  for (std::size_t index = 0; index < name.size (); ++index) {
    const char cur = name[index];
    const unsigned char value = static_cast<unsigned char> (cur);

    if (cur == '_' || cur == '-' || std::isspace (value)) {
      if (!out.empty () && out.back () != ' ') {
        out.push_back (' ');
      }
      capitalize_next = true;
      continue;
    }

    bool const split_camel_case
        = index > 0 && !capitalize_next && (std::isupper (value) != 0)
          && (std::islower (static_cast<unsigned char> (name[index - 1])) != 0);
    if (split_camel_case && !out.empty () && out.back () != ' ') {
      out.push_back (' ');
    }

    if (capitalize_next && std::isalpha (value)) {
      out.push_back (static_cast<char> (std::toupper (value)));
    } else {
      out.push_back (cur);
    }

    capitalize_next = false;
  }

  if (out.empty ()) {
    return std::string (name);
  }

  return out;
}

/**
 * Creates reflected type metadata with editor-facing labels.
 * :param T: Type being reflected.
 * :param type_id: Stable runtime reflection identifier.
 * :param display_name: Optional display name for the inspector.
 * :param description: Optional tooltip text for the inspector.
 * :param icon_path: Optional icon path for component headers.
 * :return: A meta factory ready for additional field registrations.
 */
template <typename T>
inline entt::meta_factory<T>
reflect_type (entt::id_type type_id, std::string_view display_name = {},
              std::string_view description = {},
              std::string_view icon_path = {})
{
  meta_info info{};
  info.display_name = display_name.empty ()
                          ? humanize_identifier (type_traits<T>::name ())
                          : std::string (display_name);
  info.description = std::string (description);
  info.icon_path = std::string (icon_path);

  return entt::meta_factory<T> ().type (type_id).template custom<meta_info> (
      std::move (info));
}

/**
 * Reflects a data member and assigns a default inspector label.
 * :param T: Owning reflected type.
 * :param Member: Pointer to the reflected data member.
 * :param factory: Active meta factory for the owning type.
 * :param identifier: Stable identifier used for reflection lookup.
 * :param display_name: Optional display name for the inspector.
 * :param description: Optional tooltip text for the inspector.
 * :return: The updated meta factory so calls can remain chained.
 */
template <typename T, auto Member>
inline entt::meta_factory<T>
reflect_field (entt::meta_factory<T> factory, std::string_view identifier,
               std::string_view display_name = {},
               std::string_view description = {})
{
  meta_info info{};
  info.display_name = display_name.empty () ? humanize_identifier (identifier)
                                            : std::string (display_name);
  info.description = std::string (description);

  return factory
      .template data<Member> (
          entt::hashed_string::value (identifier.data (), identifier.size ()))
      .template custom<meta_info> (std::move (info));
}

inline std::optional<meta_info>
get_meta_info (const entt::meta_type &meta)
{
  if (!meta) {
    return std::nullopt;
  }

  if (const auto *info = meta.custom ().operator const meta_info *()) {
    return *info;
  }

  return std::nullopt;
}

inline std::optional<meta_info>
get_meta_info (const entt::meta_data &meta)
{
  if (!meta) {
    return std::nullopt;
  }

  if (const auto *info = meta.custom ().operator const meta_info *()) {
    return *info;
  }

  return std::nullopt;
}

inline std::string
meta_display_name (const entt::meta_type &meta, std::string_view fallback = {})
{
  if (const auto info = get_meta_info (meta);
      info && !info->display_name.empty ()) {
    return info->display_name;
  }

  if (meta) {
    return std::string (meta.info ().name ());
  }

  return std::string (fallback);
}

inline std::string
meta_display_name (const entt::meta_data &meta, std::string_view fallback = {})
{
  if (const auto info = get_meta_info (meta);
      info && !info->display_name.empty ()) {
    return info->display_name;
  }

  return std::string (fallback);
}

inline std::string
meta_icon_path (const entt::meta_type &meta)
{
  if (const auto info = get_meta_info (meta);
      info && !info->icon_path.empty ()) {
    return info->icon_path;
  }

  return "";
}

} // namespace comp

} // namespace wsl
