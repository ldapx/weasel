// Per-viewport environment resolution, component-driven mode and viewport
// tree helpers (spec §§6.2–6.4, TODO T1). Headless: no GPU required.

#define DOCTEST_CONFIG_USE_STD_HEADERS

#include <doctest/doctest.h>

#include "wsl/comp/hierarchy.hpp"
#include "wsl/comp/singl/rendering_manager.hpp"
#include "wsl/comp/subviewport.hpp"
#include "wsl/comp/transform.hpp"
#include "wsl/comp/transform_2d.hpp"
#include "wsl/comp/viewport_environment.hpp"

using wsl::comp::find_nearest_viewport;
using wsl::comp::find_parent_viewport;
using wsl::comp::resolve_viewport_environment;
using wsl::comp::subviewport;
using wsl::comp::subviewport_mode;
using wsl::comp::subviewport_render_mode;
using wsl::comp::singl::rendering_manager;

namespace
{

entt::entity
make_entity (entt::registry &registry, entt::entity parent)
{
  entt::entity const e = registry.create ();
  registry.emplace<wsl::comp::hierarchy> (e);
  registry.get<wsl::comp::hierarchy> (e).parent = parent;
  return e;
}

} // namespace

TEST_CASE ("subviewport mode is component-driven")
{
  entt::registry registry;

  entt::entity const flat = registry.create ();
  registry.emplace<wsl::comp::transform> (flat);
  CHECK (subviewport_render_mode (registry, flat) == subviewport_mode::mode_3d);

  entt::entity const sprite = registry.create ();
  registry.emplace<wsl::comp::transform_2d> (sprite);
  CHECK (subviewport_render_mode (registry, sprite)
         == subviewport_mode::mode_2d);
}

TEST_CASE ("find_nearest_viewport walks ancestors and detects cycles")
{
  entt::registry registry;

  // Root -> vp (subviewport) -> mid -> leaf
  entt::entity const vp = make_entity (registry, entt::null);
  registry.emplace<subviewport> (vp);
  entt::entity const mid = make_entity (registry, vp);
  entt::entity const leaf = make_entity (registry, mid);

  CHECK (find_nearest_viewport (registry, leaf) == vp);
  CHECK (find_nearest_viewport (registry, mid) == vp);
  CHECK (find_nearest_viewport (registry, vp) == vp);
  CHECK (find_parent_viewport (registry, vp) == entt::null);
  CHECK (find_parent_viewport (registry, leaf) == vp);

  // A hierarchy cycle without any viewport must terminate and report root.
  entt::registry cyclic;
  entt::entity const a = make_entity (cyclic, entt::null);
  entt::entity const b = make_entity (cyclic, a);
  cyclic.get<wsl::comp::hierarchy> (a).parent = b;
  CHECK (find_nearest_viewport (cyclic, b) == entt::null);
}

TEST_CASE ("viewport environment falls back to the scene environment")
{
  entt::registry registry;
  registry.ctx ().emplace<rendering_manager> ();

  auto &scene_env = registry.ctx ().get<rendering_manager> ();
  scene_env.skybox.value = 1234;
  scene_env.ambient_intensity = 0.5F;
  scene_env.ambient_color = wsl::math::vec3f{ 0.25F, 0.5F, 0.75F };
  scene_env.exposure = 2.0F;
  scene_env.bloom_intensity = 3.0F;
  scene_env.clear_color = wsl::math::vec3f{ 0.1F, 0.2F, 0.3F };
  scene_env.clear_alpha = 0.4F;

  // Root viewport always uses the scene environment.
  {
    auto const env = resolve_viewport_environment (registry, entt::null);
    CHECK (env.skybox.value == 1234);
    CHECK (env.ambient_intensity == doctest::Approx (0.5F));
    CHECK (env.ambient_color.x () == doctest::Approx (0.25F));
    CHECK (env.exposure == doctest::Approx (2.0F));
    CHECK (env.bloom_intensity == doctest::Approx (3.0F));
    CHECK (env.clear_color.x () == doctest::Approx (0.1F));
    CHECK (env.clear_color.w () == doctest::Approx (0.4F));
  }

  // A subviewport with every `use_global` set follows the scene live.
  entt::entity const vp = registry.create ();
  registry.emplace<subviewport> (vp);

  {
    auto const env = resolve_viewport_environment (registry, vp);
    CHECK (env.skybox.value == 1234);
    CHECK (env.ambient_intensity == doctest::Approx (0.5F));
    CHECK (env.exposure == doctest::Approx (2.0F));
    CHECK (env.bloom_intensity == doctest::Approx (3.0F));
    CHECK (env.clear_color.x () == doctest::Approx (0.1F));
    CHECK (env.clear_color_enabled);
  }

  // Overrides are honored per knob; untouched knobs stay global.
  auto &sv = registry.get<subviewport> (vp);
  sv.settings.env.skybox_use_global = false;
  sv.settings.env.skybox.value = 42;
  sv.settings.env.ambient_use_global = false;
  sv.settings.env.ambient_intensity = 7.0F;
  sv.settings.env.exposure_use_global = false;
  sv.settings.env.exposure = 0.25F;
  sv.settings.env.clear_color_use_global = false;
  sv.settings.env.clear_color = wsl::math::vec4f{ 0.5F, 0.5F, 0.5F, 1.0F };
  sv.settings.env.clear_color_enabled = false;

  {
    auto const env = resolve_viewport_environment (registry, vp);
    CHECK (env.skybox.value == 42);
    CHECK (env.ambient_intensity == doctest::Approx (7.0F));
    CHECK (env.exposure == doctest::Approx (0.25F));
    CHECK (env.bloom_intensity == doctest::Approx (3.0F)); // still global
    CHECK (env.clear_color.x () == doctest::Approx (0.5F));
    CHECK (!env.clear_color_enabled);
  }

  // Two viewports with different clears resolve independently (C3).
  entt::entity const vp2 = registry.create ();
  auto &sv2 = registry.emplace<subviewport> (vp2);
  sv2.settings.env.clear_color_use_global = false;
  sv2.settings.env.clear_color = wsl::math::vec4f{ 1.0F, 0.0F, 0.0F, 1.0F };

  CHECK (resolve_viewport_environment (registry, vp).clear_color.x ()
         == doctest::Approx (0.5F));
  CHECK (resolve_viewport_environment (registry, vp2).clear_color.x ()
         == doctest::Approx (1.0F));
}

TEST_CASE ("shadow_caster excludes a viewport subtree from shadow passes")
{
  entt::registry registry;

  entt::entity const vp = make_entity (registry, entt::null);
  registry.emplace<subviewport> (vp);
  entt::entity const child = make_entity (registry, vp);

  CHECK (wsl::comp::entity_casts_shadows (registry, child));

  registry.get<subviewport> (vp).settings.shadow_caster = false;
  CHECK (!wsl::comp::entity_casts_shadows (registry, child));
  CHECK (!wsl::comp::entity_casts_shadows (registry, vp));

  // A nested viewport re-enabling casting cannot override its parent.
  entt::entity const nested = make_entity (registry, vp);
  registry.emplace<subviewport> (nested);
  registry.get<subviewport> (nested).settings.shadow_caster = true;
  CHECK (!wsl::comp::entity_casts_shadows (registry, nested));
}
