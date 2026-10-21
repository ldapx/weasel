#pragma once

#ifndef IN_MODULE_INTERFACE
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>
#endif

namespace wsl::phys
{

using body_id = std::uint64_t;
inline constexpr body_id null_body_id = 0;

inline constexpr bool
is_valid_body_id (body_id id) noexcept
{
  return id != null_body_id;
}

enum class motion_type : std::uint8_t
{
  Static,
  Kinematic,
  Dynamic
};

enum class allowed_dofs : std::uint16_t
{
  All = 0,
  TranslationX = 1U << 0U,
  TranslationY = 1U << 1U,
  TranslationZ = 1U << 2U,
  RotationX = 1U << 3U,
  RotationY = 1U << 4U,
  RotationZ = 1U << 5U
};

/** Alias used when declaring a data member named `allowed_dofs`. */
using allowed_dofs_mask = allowed_dofs;

constexpr allowed_dofs
operator| (allowed_dofs lhs, allowed_dofs rhs) noexcept
{
  return static_cast<allowed_dofs> (static_cast<std::uint16_t> (lhs)
                                    | static_cast<std::uint16_t> (rhs));
}

using object_layer = std::uint16_t;

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

enum class shape_type : std::uint8_t
{
  box,
  sphere
};

/**
 * One world-space line segment produced by debug draw.
 *
 * `rgba` holds the red/green/blue channels in 0xRRGGBB order; debug lines
 * are always drawn fully opaque.
 */
struct debug_line
{
  vector3 a{};
  vector3 b{};
  std::uint32_t rgba = 0xFFFFFFFFU;
};

struct body_desc
{
  motion_type motion = motion_type::Dynamic;
  allowed_dofs_mask allowed_dofs = allowed_dofs::All;
  object_layer layer = 0;
  shape_type shape = shape_type::box;
  vector3 position{};
  quaternion rotation{};
  vector3 half_extents{ 0.5F, 0.5F, 0.5F };
  float radius = 0.5F;
  float density = 1000.0F;
  float friction = 0.2F;
  float restitution = 0.0F;
  bool sensor = false;
};

struct sensor_overlap_event
{
  body_id sensor = null_body_id;
  body_id other = null_body_id;
  bool entered = false;
};

class engine
{
public:
  engine ();
  ~engine ();

  void step (double dt);
  void clear ();

  body_id create_body (const body_desc &desc);
  void on_remove_body (body_id id);
  bool is_body_valid (body_id id) const;
  vector3 get_body_position (body_id id) const;
  quaternion get_body_rotation (body_id id) const;
  void set_body_transform (body_id id, vector3 position, quaternion rotation);
  void set_body_surface_properties (body_id id, float friction,
                                    float restitution, object_layer layer);
  float body_surface_friction (body_id id) const;
  float body_surface_restitution (body_id id) const;
  vector3 get_body_velocity (body_id id) const;
  void set_body_velocity (body_id id, vector3 velocity);
  void add_force (body_id id, vector3 force);
  void add_impulse (body_id id, vector3 impulse);

  /**
   * Appends collider wireframes and contact geometry to `out`.
   *
   * The lines are expressed in world space, one segment per entry, ready to
   * be handed to the renderer's debug line pass. Calling this does not
   * modify the simulation.
   */
  void draw_debug (std::vector<debug_line> &out);

  double get_gravity () const;
  void set_gravity (double gravity);
  double get_fixed_step () const;
  void set_fixed_step (double step);
  double get_max_frame_time () const;
  void set_max_frame_time (double max_dt);
  int get_max_substeps () const;
  void set_max_substeps (int max_steps);

  void register_sensor (body_id id);
  void unregister_sensor (body_id id);
  bool is_sensor (body_id id) const;
  void push_sensor_event (const sensor_overlap_event &ev);
  std::vector<sensor_overlap_event> drain_sensor_events ();

private:
  struct impl;
  std::unique_ptr<impl> m_impl;

  double m_accumulator = 0.0;
  double m_gravity_y = -9.8;
  double m_fixed_step = 1.0 / 60.0;
  double m_max_frame_time = 0.25;
  int m_max_substeps = 5;

  std::unordered_set<body_id> m_sensors;
  std::mutex m_sensor_evt_mtx;
  std::vector<sensor_overlap_event> m_sensor_events;
};

} // namespace wsl::phys
