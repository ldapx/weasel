#if WSL_HAS_BOX3D

namespace wsl::phys::box3d
{

struct world::impl
{
  b3WorldId id = b3_nullWorldId;
  std::vector<body_handle> bodies;

  ~impl ()
  {
    if (b3World_IsValid (id)) {
      b3DestroyWorld (id);
    }
  }
};

namespace
{

b3Vec3
to_box3d (vector3 value)
{
  return { value.x, value.y, value.z };
}

b3Quat
to_box3d (quaternion value)
{
  return { { value.x, value.y, value.z }, value.w };
}

vector3
from_box3d (b3Vec3 value)
{
  return { value.x, value.y, value.z };
}

quaternion
from_box3d (b3Quat value)
{
  return { value.v.x, value.v.y, value.v.z, value.s };
}

b3BodyId
decode_body (body_handle body)
{
  return b3LoadBodyId (body);
}

} // namespace

namespace
{

/** Number of segments used to approximate a circle. */
constexpr int k_circle_segments = 16;

/**
 * Box3D packs a debug material preset into the high byte of a debug color;
 * the low 24 bits are the RGB channels the renderer wants.
 */
std::uint32_t
to_rgba (b3HexColor color)
{
  return static_cast<std::uint32_t> (color) & 0x00FFFFFFU;
}

b3Vec3
scale (b3Vec3 value, float factor)
{
  return { value.x * factor, value.y * factor, value.z * factor };
}

void
push_segment (std::vector<debug_line> &out, b3Vec3 a, b3Vec3 b,
              std::uint32_t rgba)
{
  out.push_back (debug_line{ { a.x, a.y, a.z }, { b.x, b.y, b.z }, rgba });
}

b3Vec3
to_world (const b3WorldTransform &transform, b3Vec3 local)
{
  return b3Add (b3RotateVector (transform.q, local), transform.p);
}

void
push_local_segment (std::vector<debug_line> &out,
                    const b3WorldTransform &transform, b3Vec3 a, b3Vec3 b,
                    std::uint32_t rgba)
{
  push_segment (out, to_world (transform, a), to_world (transform, b), rgba);
}

/** Appends a circle in the plane spanned by `axis_u` and `axis_v`. */
void
push_circle (std::vector<debug_line> &out, b3Vec3 center, b3Vec3 axis_u,
             b3Vec3 axis_v, float radius, std::uint32_t rgba)
{
  const float two_pi = 6.28318530718F;
  auto point_at = [&] (int step) {
    const float angle
        = two_pi * static_cast<float> (step) / k_circle_segments;
    const float cosine = std::cos (angle);
    const float sine = std::sin (angle);
    const b3Vec3 direction
        = b3Add (scale (axis_u, cosine), scale (axis_v, sine));
    return b3Add (center, scale (direction, radius));
  };

  b3Vec3 previous = point_at (0);
  for (int step = 1; step <= k_circle_segments; ++step) {
    const b3Vec3 current = point_at (step);
    push_segment (out, previous, current, rgba);
    previous = current;
  }
}

/** Appends the 12 edges of an oriented box. */
void
push_box (std::vector<debug_line> &out, const b3WorldTransform &transform,
          b3Vec3 half_extents, std::uint32_t rgba)
{
  b3Vec3 corners[8];
  for (int i = 0; i < 8; ++i) {
    corners[i] = b3Vec3{ ((i & 1) != 0 ? 1.0F : -1.0F) * half_extents.x,
                         ((i & 2) != 0 ? 1.0F : -1.0F) * half_extents.y,
                         ((i & 4) != 0 ? 1.0F : -1.0F) * half_extents.z };
  }

  static constexpr int edges[12][2] = {
    { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, // along x
    { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, // along y
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }  // along z
  };

  for (const auto &edge : edges) {
    push_local_segment (out, transform, corners[edge[0]], corners[edge[1]],
                        rgba);
  }
}

void
push_sphere (std::vector<debug_line> &out, const b3WorldTransform &transform,
             b3Vec3 local_center, float radius, std::uint32_t rgba)
{
  const b3Vec3 center = to_world (transform, local_center);
  push_circle (out, center, { 1.0F, 0.0F, 0.0F }, { 0.0F, 1.0F, 0.0F },
               radius, rgba);
  push_circle (out, center, { 1.0F, 0.0F, 0.0F }, { 0.0F, 0.0F, 1.0F },
               radius, rgba);
  push_circle (out, center, { 0.0F, 1.0F, 0.0F }, { 0.0F, 0.0F, 1.0F },
               radius, rgba);
}

void
push_capsule (std::vector<debug_line> &out,
              const b3WorldTransform &transform, b3Vec3 local_a,
              b3Vec3 local_b, float radius, std::uint32_t rgba)
{
  const b3Vec3 a = to_world (transform, local_a);
  const b3Vec3 b = to_world (transform, local_b);
  const b3Vec3 axis = b3Sub (b, a);
  const float length = b3Length (axis);
  if (length < 1.0e-5F) {
    push_sphere (out, transform, local_a, radius, rgba);
    return;
  }

  const b3Vec3 direction = scale (axis, 1.0F / length);
  // Any unit vector that is not parallel to the capsule axis.
  const b3Vec3 helper
      = std::fabs (direction.x) < 0.9F ? b3Vec3{ 1.0F, 0.0F, 0.0F }
                                       : b3Vec3{ 0.0F, 1.0F, 0.0F };
  const b3Vec3 side = b3Normalize (b3Cross (direction, helper));
  const b3Vec3 other = b3Cross (direction, side);

  push_circle (out, a, side, other, radius, rgba);
  push_circle (out, b, side, other, radius, rgba);
  push_segment (out, b3Add (a, scale (side, radius)),
                b3Add (b, scale (side, radius)), rgba);
  push_segment (out, b3Sub (a, scale (side, radius)),
                b3Sub (b, scale (side, radius)), rgba);
  push_segment (out, b3Add (a, scale (other, radius)),
                b3Add (b, scale (other, radius)), rgba);
  push_segment (out, b3Sub (a, scale (other, radius)),
                b3Sub (b, scale (other, radius)), rgba);
}

/** Wireframe of a convex hull, straight from its half-edge structure. */
void
push_hull (std::vector<debug_line> &out, const b3WorldTransform &transform,
           const b3HullData *hull, std::uint32_t rgba)
{
  const b3Vec3 *points = b3GetHullPoints (hull);
  const b3HullHalfEdge *edges = b3GetHullEdges (hull);
  if ((points == nullptr) || (edges == nullptr)) {
    return;
  }

  for (int i = 0; i < hull->edgeCount; ++i) {
    const b3HullHalfEdge &edge = edges[i];
    // Half-edges are stored in twin pairs; only the lower index draws, so
    // every edge of the hull is emitted exactly once.
    if (edge.twin < i) {
      continue;
    }
    if (edge.next >= hull->edgeCount) {
      continue;
    }
    push_local_segment (out, transform, points[edge.origin],
                        points[edges[edge.next].origin], rgba);
  }
}

/** AABB wireframe used as the fallback for shapes without a wireframe. */
void
push_bounds (std::vector<debug_line> &out, b3AABB bounds, std::uint32_t rgba)
{
  b3WorldTransform transform{};
  transform.p = scale (b3Add (bounds.lowerBound, bounds.upperBound), 0.5F);
  transform.q = { { 0.0F, 0.0F, 0.0F }, 1.0F };
  push_box (out, transform,
            scale (b3Sub (bounds.upperBound, bounds.lowerBound), 0.5F), rgba);
}

/**
 * Box3D reports shapes through the descriptor we copied in
 * create_debug_shape; tessellate it into line segments.
 */
bool
debug_draw_shape (void *user_shape, b3WorldTransform transform,
                   b3HexColor color, void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  const auto *shape = static_cast<const b3DebugShape *> (user_shape);
  if (shape == nullptr) {
    return true;
  }

  const std::uint32_t rgba = to_rgba (color);
  switch (shape->type) {
    case b3_hullShape:
      if (shape->hull != nullptr) {
        push_hull (out, transform, shape->hull, rgba);
      }
      break;
    case b3_sphereShape:
      if (shape->sphere != nullptr) {
        push_sphere (out, transform, shape->sphere->center,
                     shape->sphere->radius, rgba);
      }
      break;
    case b3_capsuleShape:
      if (shape->capsule != nullptr) {
        push_capsule (out, transform, shape->capsule->center1,
                      shape->capsule->center2, shape->capsule->radius, rgba);
      }
      break;
    default:
      // Compound, mesh, and height field shapes have no wireframe yet.
      break;
  }
  return true;
}

void
debug_draw_segment (b3Pos p1, b3Pos p2, b3HexColor color, void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  push_segment (out,
                { static_cast<float> (p1.x), static_cast<float> (p1.y),
                  static_cast<float> (p1.z) },
                { static_cast<float> (p2.x), static_cast<float> (p2.y),
                  static_cast<float> (p2.z) },
                to_rgba (color));
}

void
debug_draw_point (b3Pos p, float size, b3HexColor color, void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  const std::uint32_t rgba = to_rgba (color);
  const b3Vec3 center{ static_cast<float> (p.x), static_cast<float> (p.y),
                       static_cast<float> (p.z) };
  const float half = size > 0.0F ? 0.5F * size : 0.05F;
  push_segment (out, b3Sub (center, { half, 0.0F, 0.0F }),
                b3Add (center, { half, 0.0F, 0.0F }), rgba);
  push_segment (out, b3Sub (center, { 0.0F, half, 0.0F }),
                b3Add (center, { 0.0F, half, 0.0F }), rgba);
  push_segment (out, b3Sub (center, { 0.0F, 0.0F, half }),
                b3Add (center, { 0.0F, 0.0F, half }), rgba);
}

void
debug_draw_bounds (b3AABB bounds, b3HexColor color, void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  push_bounds (out, bounds, to_rgba (color));
}

void
debug_draw_box (b3Vec3 extents, b3WorldTransform transform, b3HexColor color,
                void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  push_box (out, transform, extents, to_rgba (color));
}

void
debug_draw_sphere (b3Pos p, float radius, b3HexColor color, float /*alpha*/,
                   void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  const std::uint32_t rgba = to_rgba (color);
  const b3WorldTransform transform{
    { static_cast<float> (p.x), static_cast<float> (p.y),
      static_cast<float> (p.z) },
    { { 0.0F, 0.0F, 0.0F }, 1.0F }
  };
  push_sphere (out, transform, { 0.0F, 0.0F, 0.0F }, radius, rgba);
}

void
debug_draw_capsule (b3Pos p1, b3Pos p2, float radius, b3HexColor color,
                    float /*alpha*/, void *context)
{
  auto &out = *static_cast<std::vector<debug_line> *> (context);
  push_capsule (out, b3WorldTransform_identity,
                { static_cast<float> (p1.x), static_cast<float> (p1.y),
                  static_cast<float> (p1.z) },
                { static_cast<float> (p2.x), static_cast<float> (p2.y),
                  static_cast<float> (p2.z) },
                radius, to_rgba (color));
}

/**
 * Box3D only reports shapes to b3World_Draw when the world definition can
 * create a debug shape, and it hands that pointer straight back to
 * DrawShapeFcn. The descriptor it passes in is transient, so keep a copy.
 * The hull/sphere payloads it points at stay alive until Box3D calls
 * destroy_debug_shape.
 */
void *
create_debug_shape (const b3DebugShape *shape, void * /*context*/)
{
  return shape == nullptr ? nullptr : new b3DebugShape (*shape);
}

void
destroy_debug_shape (void *user_shape, void * /*context*/)
{
  delete static_cast<b3DebugShape *> (user_shape);
}

} // namespace

world::world (float gravity_y, std::uint32_t worker_count)
  : m_impl (std::make_unique<impl> ())
{
  b3WorldDef definition = b3DefaultWorldDef ();
  definition.gravity = { 0.0F, gravity_y, 0.0F };
  definition.workerCount = worker_count == 0 ? 1 : worker_count;
  // Without these Box3D reports no shapes at all from b3World_Draw.
  definition.createDebugShape = create_debug_shape;
  definition.destroyDebugShape = destroy_debug_shape;
  m_impl->id = b3CreateWorld (&definition);
}

world::~world () = default;

world::world (world &&other) noexcept = default;

world &
world::operator= (world &&other) noexcept = default;

void
world::clear ()
{
  if (valid ()) {
    for (body_handle body : m_impl->bodies) {
      if (b3Body_IsValid (decode_body (body))) {
        b3DestroyBody (decode_body (body));
      }
    }
    m_impl->bodies.clear ();
  }
}

std::vector<sensor_event>
world::drain_sensor_events ()
{
  std::vector<sensor_event> result;
  if (!valid ()) {
    return result;
  }

  const b3SensorEvents events = b3World_GetSensorEvents (m_impl->id);
  auto convert = [&] (b3ShapeId sensor_shape, b3ShapeId visitor_shape,
                      bool entered) {
    if (!b3Shape_IsValid (sensor_shape) || !b3Shape_IsValid (visitor_shape)) {
      return;
    }
    result.push_back (
        { b3StoreBodyId (b3Shape_GetBody (sensor_shape)),
          b3StoreBodyId (b3Shape_GetBody (visitor_shape)), entered });
  };
  for (int i = 0; i < events.beginCount; ++i) {
    convert (events.beginEvents[i].sensorShapeId,
             events.beginEvents[i].visitorShapeId, true);
  }
  for (int i = 0; i < events.endCount; ++i) {
    convert (events.endEvents[i].sensorShapeId,
             events.endEvents[i].visitorShapeId, false);
  }
  return result;
}

bool
world::valid () const noexcept
{
  return m_impl != nullptr && b3World_IsValid (m_impl->id);
}

void
world::step (float time_step, int sub_step_count)
{
  if (valid () && time_step > 0.0F && sub_step_count > 0) {
    b3World_Step (m_impl->id, time_step, sub_step_count);
  }
}

void
world::set_gravity (vector3 value)
{
  if (valid ()) {
    b3World_SetGravity (m_impl->id, to_box3d (value));
  }
}

vector3
world::gravity () const
{
  return valid () ? from_box3d (b3World_GetGravity (m_impl->id)) : vector3{};
}

body_handle
world::create_body (const body_desc &desc)
{
  if (!valid ()) {
    return null_body;
  }

  b3BodyDef body_definition = b3DefaultBodyDef ();
  body_definition.type = static_cast<b3BodyType> (desc.type);
  body_definition.position = to_box3d (desc.position);
  body_definition.rotation = to_box3d (desc.rotation);

  const b3BodyId body = b3CreateBody (m_impl->id, &body_definition);
  if (!b3Body_IsValid (body)) {
    return null_body;
  }

  b3ShapeDef shape_definition = b3DefaultShapeDef ();
  shape_definition.density = desc.density;
  shape_definition.baseMaterial.friction = desc.friction;
  shape_definition.baseMaterial.restitution = desc.restitution;
  shape_definition.isSensor = desc.sensor;
  shape_definition.enableSensorEvents = desc.sensor;

  b3ShapeId shape = b3_nullShapeId;
  if (desc.shape == shape_type::box) {
    const b3BoxHull hull = b3MakeBoxHull (
        desc.half_extents.x, desc.half_extents.y, desc.half_extents.z);
    shape = b3CreateHullShape (body, &shape_definition, &hull.base);
  } else {
    const b3Sphere sphere{ { 0.0F, 0.0F, 0.0F }, desc.radius };
    shape = b3CreateSphereShape (body, &shape_definition, &sphere);
  }

  if (!b3Shape_IsValid (shape)) {
    b3DestroyBody (body);
    return null_body;
  }

  b3Body_ApplyMassFromShapes (body);
  const body_handle handle = b3StoreBodyId (body);
  m_impl->bodies.push_back (handle);
  return handle;
}

void
world::destroy_body (body_handle body)
{
  if (is_body_valid (body)) {
    b3DestroyBody (decode_body (body));
    auto it = std::find (m_impl->bodies.begin (), m_impl->bodies.end (), body);
    if (it != m_impl->bodies.end ()) {
      m_impl->bodies.erase (it);
    }
  }
}

bool
world::is_body_valid (body_handle body) const
{
  return valid () && body != null_body
         && b3Body_IsValid (decode_body (body));
}

vector3
world::body_position (body_handle body) const
{
  return is_body_valid (body)
             ? from_box3d (b3Body_GetPosition (decode_body (body)))
             : vector3{};
}

quaternion
world::body_rotation (body_handle body) const
{
  return is_body_valid (body)
             ? from_box3d (b3Body_GetRotation (decode_body (body)))
             : quaternion{};
}

void
world::set_body_transform (body_handle body, vector3 position,
                           quaternion rotation)
{
  if (is_body_valid (body)) {
    b3Body_SetTransform (decode_body (body), to_box3d (position),
                         to_box3d (rotation));
  }
}

namespace
{

/**
 * Every body built by this adapter owns exactly one shape, so surface
 * tweaks address the body's first shape. Returns a null shape id when the
 * body is invalid or shapeless; callers must check with b3Shape_IsValid.
 */
b3ShapeId
first_body_shape (b3BodyId body)
{
  b3ShapeId shape = b3_nullShapeId;
  if (b3Body_IsValid (body) && b3Body_GetShapes (body, &shape, 1) == 1) {
    return shape;
  }
  return b3_nullShapeId;
}

} // namespace

void
world::set_body_surface_properties (body_handle body, float friction,
                                    float restitution)
{
  if (!is_body_valid (body)) {
    return;
  }
  const b3ShapeId shape = first_body_shape (decode_body (body));
  if (b3Shape_IsValid (shape)) {
    b3Shape_SetFriction (shape, friction);
    b3Shape_SetRestitution (shape, restitution);
  }
}

float
world::body_surface_friction (body_handle body) const
{
  if (!is_body_valid (body)) {
    return 0.2F;
  }
  const b3ShapeId shape = first_body_shape (decode_body (body));
  return b3Shape_IsValid (shape) ? b3Shape_GetFriction (shape) : 0.2F;
}

float
world::body_surface_restitution (body_handle body) const
{
  if (!is_body_valid (body)) {
    return 0.0F;
  }
  const b3ShapeId shape = first_body_shape (decode_body (body));
  return b3Shape_IsValid (shape) ? b3Shape_GetRestitution (shape) : 0.0F;
}

vector3
world::body_linear_velocity (body_handle body) const
{
  return is_body_valid (body)
             ? from_box3d (b3Body_GetLinearVelocity (decode_body (body)))
             : vector3{};
}

void
world::set_body_linear_velocity (body_handle body, vector3 velocity)
{
  if (is_body_valid (body)) {
    b3Body_SetLinearVelocity (decode_body (body), to_box3d (velocity));
  }
}

void
world::add_force (body_handle body, vector3 force)
{
  if (is_body_valid (body)) {
    b3Body_ApplyForceToCenter (decode_body (body), to_box3d (force), true);
  }
}void
world::add_impulse (body_handle body, vector3 impulse)
{
  if (is_body_valid (body)) {
    b3Body_ApplyLinearImpulseToCenter (decode_body (body),
                                       to_box3d (impulse), true);
  }
}

void
world::draw (std::vector<debug_line> &out)
{
  if (!valid ()) {
    return;
  }

  b3DebugDraw draw = b3DefaultDebugDraw ();
  draw.DrawShapeFcn = debug_draw_shape;
  draw.DrawSegmentFcn = debug_draw_segment;
  draw.DrawBoundsFcn = debug_draw_bounds;
  draw.DrawBoxFcn = debug_draw_box;
  draw.DrawSphereFcn = debug_draw_sphere;
  draw.DrawCapsuleFcn = debug_draw_capsule;
  draw.DrawPointFcn = debug_draw_point;
  draw.drawShapes = true;
  draw.drawContacts = true;
  draw.drawJoints = true;
  draw.context = &out;

  b3World_Draw (m_impl->id, &draw, ~0ULL);
}

} // namespace wsl::phys::box3d

#else

namespace wsl::phys::box3d
{

struct world::impl
{
};

world::world (float, std::uint32_t) : m_impl (std::make_unique<impl> ())
{
}

world::~world () = default;
world::world (world &&other) noexcept = default;
world &
world::operator= (world &&other) noexcept = default;
void
world::clear ()
{
}
std::vector<sensor_event>
world::drain_sensor_events ()
{
  return {};
}
bool
world::valid () const noexcept
{
  return false;
}
void
world::step (float, int)
{
}
void
world::set_gravity (vector3)
{
}
vector3
world::gravity () const
{
  return {};
}
body_handle
world::create_body (const body_desc &)
{
  return null_body;
}
void
world::destroy_body (body_handle)
{
}
bool
world::is_body_valid (body_handle) const
{
  return false;
}
vector3
world::body_position (body_handle) const
{
  return {};
}
quaternion
world::body_rotation (body_handle) const
{
  return {};
}
void
world::set_body_transform (body_handle, vector3, quaternion)
{
}
void
world::add_force (body_handle, vector3)
{
}
void
world::add_impulse (body_handle, vector3)
{
}
void
world::draw (std::vector<debug_line> &)
{
}

} // namespace wsl::phys::box3d

#endif
