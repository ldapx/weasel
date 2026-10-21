#include "physics_engine.hpp"

#include "box3d_adapter.hpp"
#include "wsl/log/log.hpp"

#include <algorithm>
#include <memory>

namespace wsl::phys
{

struct engine::impl
{
  box3d::world world;
};

namespace
{

box3d::body_desc
to_box3d_desc (const body_desc &desc)
{
  box3d::body_desc out;
  out.type = static_cast<box3d::body_type> (desc.motion);
  out.shape = static_cast<box3d::shape_type> (desc.shape);
  out.position = { desc.position.x, desc.position.y, desc.position.z };
  out.rotation
      = { desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w };
  out.half_extents
      = { desc.half_extents.x, desc.half_extents.y, desc.half_extents.z };
  out.radius = desc.radius;
  out.density = desc.density;
  out.friction = desc.friction;
  out.restitution = desc.restitution;
  out.sensor = desc.sensor;
  return out;
}

box3d::vector3
to_box3d (vector3 value)
{
  return { value.x, value.y, value.z };
}

vector3
from_box3d (box3d::vector3 value)
{
  return { value.x, value.y, value.z };
}

box3d::quaternion
to_box3d (quaternion value)
{
  return { value.x, value.y, value.z, value.w };
}

quaternion
from_box3d (box3d::quaternion value)
{
  return { value.x, value.y, value.z, value.w };
}

} // namespace

engine::engine () : m_impl (std::make_unique<impl> ())
{
  m_impl->world.set_gravity ({ 0.0F, static_cast<float> (m_gravity_y), 0.0F });
  wsl::log::phys ()->debug ("Box3D physics backend initialized");
}

engine::~engine () = default;

void
engine::step (double dt)
{
  dt = std::min (dt, m_max_frame_time);
  m_accumulator += dt;
  int steps = 0;
  while (m_accumulator >= m_fixed_step && steps < m_max_substeps) {
    m_impl->world.step (static_cast<float> (m_fixed_step), 1);
    for (const auto &event : m_impl->world.drain_sensor_events ()) {
      push_sensor_event ({ event.sensor, event.other, event.entered });
    }
    m_accumulator -= m_fixed_step;
    ++steps;
  }
}

void
engine::clear ()
{
  m_impl->world.clear ();
  m_sensors.clear ();
  std::scoped_lock const lock (m_sensor_evt_mtx);
  m_sensor_events.clear ();
  m_accumulator = 0.0;
}

body_id
engine::create_body (const body_desc &desc)
{
  const body_id id = m_impl->world.create_body (to_box3d_desc (desc));
  if (id != null_body_id && desc.sensor) {
    register_sensor (id);
  }
  return id;
}

void
engine::on_remove_body (body_id id)
{
  if (!is_body_valid (id)) {
    return;
  }
  unregister_sensor (id);
  m_impl->world.destroy_body (id);
}

bool
engine::is_body_valid (body_id id) const
{
  return m_impl->world.is_body_valid (id);
}

vector3
engine::get_body_position (body_id id) const
{
  if (!is_body_valid (id)) {
    return {};
  }
  return from_box3d (m_impl->world.body_position (id));
}

quaternion
engine::get_body_rotation (body_id id) const
{
  if (!is_body_valid (id)) {
    return {};
  }
  return from_box3d (m_impl->world.body_rotation (id));
}

void
engine::set_body_transform (body_id id, vector3 position, quaternion rotation)
{
  if (!is_body_valid (id)) {
    return;
  }
  m_impl->world.set_body_transform (id, to_box3d (position),
                                    to_box3d (rotation));
}

void
engine::set_body_surface_properties (body_id id, float friction,
                                     float restitution, object_layer layer)
{
  if (!is_body_valid (id)) {
    return;
  }
  // Collision layer/mask filtering is not mapped to the backend yet (see
  // the area-overlap/layer item in DASLANG_API_OVERHAUL_PLAN.md); friction
  // and restitution apply to the body's shape immediately.
  (void)layer;
  m_impl->world.set_body_surface_properties (id, friction, restitution);
}

float
engine::body_surface_friction (body_id id) const
{
  if (!is_body_valid (id)) {
    return 0.2F;
  }
  return m_impl->world.body_surface_friction (id);
}

float
engine::body_surface_restitution (body_id id) const
{
  if (!is_body_valid (id)) {
    return 0.0F;
  }
  return m_impl->world.body_surface_restitution (id);
}

vector3
engine::get_body_velocity (body_id id) const
{
  if (!is_body_valid (id)) {
    return {};
  }
  return from_box3d (m_impl->world.body_linear_velocity (id));
}

void
engine::set_body_velocity (body_id id, vector3 velocity)
{
  if (!is_body_valid (id)) {
    return;
  }
  m_impl->world.set_body_linear_velocity (id, to_box3d (velocity));
}

void
engine::add_force (body_id id, vector3 force)
{
  if (!is_body_valid (id)) {
    return;
  }
  m_impl->world.add_force (id, to_box3d (force));
}

void
engine::add_impulse (body_id id, vector3 impulse)
{
  if (!is_body_valid (id)) {
    return;
  }
  m_impl->world.add_impulse (id, to_box3d (impulse));
}

void
engine::draw_debug (std::vector<debug_line> &out)
{
  std::vector<box3d::debug_line> backend_lines;
  m_impl->world.draw (backend_lines);

  out.reserve (out.size () + backend_lines.size ());
  for (const box3d::debug_line &line : backend_lines) {
    out.push_back (debug_line{ { line.a.x, line.a.y, line.a.z },
                               { line.b.x, line.b.y, line.b.z },
                               line.rgba });
  }
}

double
engine::get_gravity () const
{
  return m_gravity_y;
}
void
engine::set_gravity (double gravity)
{
  m_gravity_y = gravity;
  m_impl->world.set_gravity ({ 0.0F, static_cast<float> (gravity), 0.0F });
}
double
engine::get_fixed_step () const
{
  return m_fixed_step;
}
void
engine::set_fixed_step (double step)
{
  m_fixed_step = std::max (step, 1.0e-4);
}
double
engine::get_max_frame_time () const
{
  return m_max_frame_time;
}
void
engine::set_max_frame_time (double max_dt)
{
  m_max_frame_time = std::max (max_dt, m_fixed_step);
}
int
engine::get_max_substeps () const
{
  return m_max_substeps;
}
void
engine::set_max_substeps (int max_steps)
{
  m_max_substeps = std::max (max_steps, 1);
}

void
engine::register_sensor (body_id id)
{
  if (is_body_valid (id)) {
    m_sensors.insert (id);
  }
}
void
engine::unregister_sensor (body_id id)
{
  m_sensors.erase (id);
}
bool
engine::is_sensor (body_id id) const
{
  return is_valid_body_id (id) && m_sensors.contains (id);
}
void
engine::push_sensor_event (const sensor_overlap_event &event)
{
  std::scoped_lock const lock (m_sensor_evt_mtx);
  m_sensor_events.push_back (event);
}
std::vector<sensor_overlap_event>
engine::drain_sensor_events ()
{
  std::scoped_lock const lock (m_sensor_evt_mtx);
  std::vector<sensor_overlap_event> result;
  result.swap (m_sensor_events);
  return result;
}

} // namespace wsl::phys
