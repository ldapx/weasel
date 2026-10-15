#pragma once

#include "../comp/component_meta.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <glm/glm.hpp>
#endif
#ifndef IN_MODULE_INTERFACE
#include <glm/gtc/quaternion.hpp>
#endif

namespace wsl
{

/**
 * Common math types and vector utilities.
 */
namespace math
{

struct vec2f
{
  vec2f () = default;
  vec2f (float x_val, float y_val) : m_x (x_val), m_y (y_val) {}
  vec2f (const glm::vec2 &v) : m_x (v.x), m_y (v.y) {}

  float
  x () const
  {
    return m_x;
  }
  float &
  x ()
  {
    return m_x;
  }
  float
  y () const
  {
    return m_y;
  }
  float &
  y ()
  {
    return m_y;
  }

  operator glm::vec2 () const { return glm::vec2{ m_x, m_y }; }

  bool
  operator== (const vec2f &other) const
  {
    return m_x == other.m_x && m_y == other.m_y;
  }
  bool
  operator!= (const vec2f &other) const
  {
    return !(*this == other);
  }

  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    entt::meta_factory<vec2f> ()
        .type (entt::type_hash<vec2f>::value ())
        .data<&vec2f::m_x> ("x"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "x", "X Coordinate", "" })
        .data<&vec2f::m_y> ("y"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "y", "Y Coordinate", "" });
  }

private:
  float m_x{ 0 }, m_y{ 0 };
};

struct vec3f
{
  vec3f () = default;
  vec3f (float x_val, float y_val, float z_val)
      : m_x (x_val), m_y (y_val), m_z (z_val)
  {
  }
  vec3f (const glm::vec3 &v) : m_x (v.x), m_y (v.y), m_z (v.z) {}

  float
  x () const
  {
    return m_x;
  }
  float &
  x ()
  {
    return m_x;
  }
  float
  y () const
  {
    return m_y;
  }
  float &
  y ()
  {
    return m_y;
  }
  float
  z () const
  {
    return m_z;
  }
  float &
  z ()
  {
    return m_z;
  }

  operator glm::vec3 () const { return glm::vec3{ m_x, m_y, m_z }; }

  bool
  operator== (const vec3f &other) const
  {
    return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z;
  }
  bool
  operator!= (const vec3f &other) const
  {
    return !(*this == other);
  }

  vec3f &
  operator+= (const glm::vec3 &v)
  {
    m_x += v.x;
    m_y += v.y;
    m_z += v.z;
    return *this;
  }

  vec3f &
  operator-= (const glm::vec3 &v)
  {
    m_x -= v.x;
    m_y -= v.y;
    m_z -= v.z;
    return *this;
  }

  // ---- NEW: custom inspector ----
  // Returns true if any value changed.
  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    entt::meta_factory<vec3f> ()
        .type (entt::type_hash<vec3f>::value ())

        // ---- NEW: register the custom inspector function in meta ----

        .data<&vec3f::m_x> ("x"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "x", "X Coordinate", "" })
        .data<&vec3f::m_y> ("y"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "y", "Y Coordinate", "" })
        .data<&vec3f::m_z> ("z"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "z", "Z Coordinate", "" });
  }

private:
  float m_x{ 0 }, m_y{ 0 }, m_z{ 0 };
};

struct vec4f
{
  vec4f () = default;
  vec4f (float x_val, float y_val, float z_val, float w_val)
      : m_x (x_val), m_y (y_val), m_z (z_val), m_w (w_val)
  {
  }
  vec4f (const glm::vec4 &v) : m_x (v.x), m_y (v.y), m_z (v.z), m_w (v.w) {}

  float
  x () const
  {
    return m_x;
  }
  float &
  x ()
  {
    return m_x;
  }
  float
  y () const
  {
    return m_y;
  }
  float &
  y ()
  {
    return m_y;
  }
  float
  z () const
  {
    return m_z;
  }
  float &
  z ()
  {
    return m_z;
  }
  float
  w () const
  {
    return m_w;
  }
  float &
  w ()
  {
    return m_w;
  }

  operator glm::vec4 () const { return glm::vec4{ m_x, m_y, m_z, m_w }; }

  bool
  operator== (const vec4f &other) const
  {
    return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z
           && m_w == other.m_w;
  }
  bool
  operator!= (const vec4f &other) const
  {
    return !(*this == other);
  }

  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    entt::meta_factory<vec4f> ()
        .type (entt::type_hash<vec4f>::value ())
        .data<&vec4f::m_x> ("x"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "x", "X Coordinate", "" })
        .data<&vec4f::m_y> ("y"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "y", "Y Coordinate", "" })
        .data<&vec4f::m_z> ("z"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "z", "Z Coordinate", "" })
        .data<&vec4f::m_w> ("w"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "w", "W Coordinate", "" });
  }

private:
  float m_x{ 0 }, m_y{ 0 }, m_z{ 0 }, m_w{ 0 };
};

struct quatf
{
  quatf () = default;
  quatf (float x_val, float y_val, float z_val, float w_val)
      : m_x (x_val), m_y (y_val), m_z (z_val), m_w (w_val)
  {
  }
  quatf (const glm::quat &q) : m_x (q.x), m_y (q.y), m_z (q.z), m_w (q.w) {}

  float
  x () const
  {
    return m_x;
  }
  float &
  x ()
  {
    return m_x;
  }
  float
  y () const
  {
    return m_y;
  }
  float &
  y ()
  {
    return m_y;
  }
  float
  z () const
  {
    return m_z;
  }
  float &
  z ()
  {
    return m_z;
  }
  float
  w () const
  {
    return m_w;
  }
  float &
  w ()
  {
    return m_w;
  }

  operator glm::quat () const { return glm::quat{ m_w, m_x, m_y, m_z }; }

  bool
  operator== (const quatf &other) const
  {
    return m_x == other.m_x && m_y == other.m_y && m_z == other.m_z
           && m_w == other.m_w;
  }
  bool
  operator!= (const quatf &other) const
  {
    return !(*this == other);
  }

  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    entt::meta_factory<quatf> ()
        .type (entt::type_hash<quatf>::value ())
        .data<&quatf::m_x> ("x"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "x", "X Coordinate", "" })
        .data<&quatf::m_y> ("y"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "y", "Y Coordinate", "" })
        .data<&quatf::m_z> ("z"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "z", "Z Coordinate", "" })
        .data<&quatf::m_w> ("w"_hs)
        .custom<comp::meta_info> (comp::meta_info{ "w", "W Coordinate", "" });
  }

private:
  float m_x{ 0 }, m_y{ 0 }, m_z{ 0 }, m_w{ 1 };
};
} // namespace math

} // namespace wsl
