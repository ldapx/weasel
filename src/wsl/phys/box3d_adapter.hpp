#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace wsl::phys::box3d
{

struct vector3
{
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct quaternion
{
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float w = 1.0F;
};

enum class body_type : std::uint8_t
{
  static_body,
  kinematic,
  dynamic
};

enum class shape_type : std::uint8_t
{
  box,
  sphere
};

struct body_desc
{
  body_type type = body_type::dynamic;
  shape_type shape = shape_type::box;
  vector3 position{ 0.0F, 0.0F, 0.0F };
  quaternion rotation{};
  vector3 half_extents{ 0.5F, 0.5F, 0.5F };
  float radius = 0.5F;
  float density = 1000.0F;
  float friction = 0.2F;
  float restitution = 0.0F;
  bool sensor = false;
};

using body_handle = std::uint64_t;
inline constexpr body_handle null_body = 0;

struct sensor_event
{
  body_handle sensor = null_body;
  body_handle other = null_body;
  bool entered = false;
};

/**
 * One world-space debug line segment, backend-neutral like every other type
 * in this boundary. `rgba` is 0xRRGGBB.
 */
struct debug_line
{
  vector3 a{};
  vector3 b{};
  std::uint32_t rgba = 0xFFFFFFFFU;
};

/**
 * Small Weasel-owned boundary for the Box3D C API.
 *
 * Box3D handles and headers intentionally do not appear in this interface.
 * This lets the physics implementation migrate independently of components
 * and other public engine headers.
 */
class world final
{
public:
  explicit world (float gravity_y = -9.8F, std::uint32_t worker_count = 1);
  ~world ();

  world (world &&) noexcept;
  world &operator= (world &&) noexcept;
  world (const world &) = delete;
  world &operator= (const world &) = delete;

  bool valid () const noexcept;
  void step (float time_step, int sub_step_count = 4);
  void clear ();
  std::vector<sensor_event> drain_sensor_events ();
  void set_gravity (vector3 gravity);
  vector3 gravity () const;

  body_handle create_body (const body_desc &desc);
  void destroy_body (body_handle body);
  bool is_body_valid (body_handle body) const;
  vector3 body_position (body_handle body) const;
  quaternion body_rotation (body_handle body) const;
  void set_body_transform (body_handle body, vector3 position,
                           quaternion rotation);
  void set_body_surface_properties (body_handle body, float friction,
                                    float restitution);
  float body_surface_friction (body_handle body) const;
  float body_surface_restitution (body_handle body) const;
  vector3 body_linear_velocity (body_handle body) const;
  void set_body_linear_velocity (body_handle body, vector3 velocity);
  void add_force (body_handle body, vector3 force);
  void add_impulse (body_handle body, vector3 impulse);

  /**
   * Appends collider wireframes (and contact/joint geometry) to `out` via
   * Box3D's `b3World_Draw` callbacks.
   *
   * Shapes are tessellated into line segments here because Box3D hands the
   * raw shape to the callback instead of decomposing it. Box3D needs a
   * `createDebugShape` hook in the world definition for shapes to be
   * reported at all; the adapter installs one when the world is created.
   */
  void draw (std::vector<debug_line> &out);

private:
  struct impl;
  std::unique_ptr<impl> m_impl;
};

} // namespace wsl::phys::box3d
