#pragma once

#include "../comp/component_meta.hpp"

#ifndef IN_MODULE_INTERFACE
#include <entt/entt.hpp>
#endif

#ifndef IN_MODULE_INTERFACE
#include <glm/glm.hpp>
#endif

namespace wsl
{

namespace math
{

struct mat33f
{
  /** Column-major storage: data[(col * 3) + row] */
private:
  float m_data[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };

public:
  mat33f () = default;

  mat33f (const glm::mat3 &mat)
  {
    for (int col = 0; col < 3; ++col) {
      for (int row = 0; row < 3; ++row) {
        m_data[static_cast<ptrdiff_t> ((col * 3) + row)] = mat[col][row];
      }
    }
  }

  float const *
  data () const
  {
    return m_data;
  }
  float *
  data ()
  {
    return m_data;
  }

  operator glm::mat3 () const
  {
    glm::mat3 result;
    for (int col = 0; col < 3; ++col) {
      for (int row = 0; row < 3; ++row) {
        result[col][row] = m_data[static_cast<ptrdiff_t> ((col * 3) + row)];
      }
    }
    return result;
  }

  mat33f &
  operator= (const glm::mat3 &mat)
  {
    for (int col = 0; col < 3; ++col) {
      for (int row = 0; row < 3; ++row) {
        m_data[static_cast<ptrdiff_t> ((col * 3) + row)] = mat[col][row];
      }
    }
    return *this;
  }

  bool
  operator== (const mat33f &other) const
  {
    for (int i = 0; i < 9; ++i) {
      if (m_data[i] != other.m_data[i]) {
        return false;
      }
    }
    return true;
  }
  bool
  operator!= (const mat33f &other) const
  {
    return !(*this == other);
  }

  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    auto &&factory = entt::meta_factory<mat33f> ().type (
        entt::type_hash<mat33f>::value ());
  }
};

struct mat44f
{
  /** Column-major storage: data[(col * 4) + row] */
private:
  float m_data[16]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

public:
  mat44f () = default;

  mat44f (const glm::mat4 &mat)
  {
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        m_data[static_cast<ptrdiff_t> ((col * 4) + row)] = mat[col][row];
      }
    }
  }

  float const *
  data () const
  {
    return m_data;
  }
  float *
  data ()
  {
    return m_data;
  }

  operator glm::mat4 () const
  {
    glm::mat4 result;
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        result[col][row] = m_data[static_cast<ptrdiff_t> ((col * 4) + row)];
      }
    }
    return result;
  }

  mat44f &
  operator= (const glm::mat4 &mat)
  {
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        m_data[static_cast<ptrdiff_t> ((col * 4) + row)] = mat[col][row];
      }
    }
    return *this;
  }

  /**
   * Column-major indexed access (read-only), compatible with
   * glm::mat4[col][row]
   */
  float const *
  operator[] (int col) const
  {
    return &m_data[static_cast<ptrdiff_t> (col) * 4];
  }

  /** Column-major indexed access (mutable), compatible with glm::mat4[col][row]
   */
  float *
  operator[] (int col)
  {
    return &m_data[static_cast<ptrdiff_t> (col) * 4];
  }

  bool
  operator== (const mat44f &other) const
  {
    for (int i = 0; i < 16; ++i) {
      if (m_data[i] != other.m_data[i]) {
        return false;
      }
    }
    return true;
  }
  bool
  operator!= (const mat44f &other) const
  {
    return !(*this == other);
  }

  bool custom_inspect (const char *label);

  static void
  register_meta ()
  {
    using namespace entt::literals;
    auto &&factory = entt::meta_factory<mat44f> ().type (
        entt::type_hash<mat44f>::value ());
  }
};
} // namespace math

} // namespace wsl
