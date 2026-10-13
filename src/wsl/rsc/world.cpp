#include "world.hpp"

#include "wsl/comp/singl/editor_context.hpp"
#include "../comp/singl/runtime_context_fwd.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wsl
{

rsc::world::world (comp::singl::runtime_context *runtime_ctx)
    : m_runtime_ctx (runtime_ctx)
{
}

rsc::scene &
rsc::world::create_scene (const std::string &name)
{
  scene &new_scene = *m_scenes.emplace_back (
      std::make_unique<scene> (m_runtime_ctx, m_editor_ctx, name));
  return new_scene;
}

rsc::scene &
rsc::world::add_scene (scene &&ready_scene)
{
  scene &new_scene = *m_scenes.emplace_back (
      std::make_unique<scene> (std::move (ready_scene)));
  return new_scene;
}

void
rsc::world::destroy_scene (scene *target)
{
  if (target == nullptr) {
    return;
  }

  target->stop_and_clear ();

  m_scenes.erase (
      std::remove_if (m_scenes.begin (), m_scenes.end (),
                      [&] (const std::unique_ptr<scene> &scene_ptr) {
                        return scene_ptr.get () == target;
                      }),
      m_scenes.end ());
}

void
rsc::world::set_editor_context (comp::singl::editor_context *editor_ctx)
{
  m_editor_ctx = editor_ctx;
}

comp::singl::runtime_context *
rsc::world::get_runtime_context () const
{
  return m_runtime_ctx;
}

std::vector<std::unique_ptr<rsc::scene>> &
rsc::world::get_scenes ()
{
  return m_scenes;
}

const std::vector<std::unique_ptr<rsc::scene>> &
rsc::world::get_scenes () const
{
  return m_scenes;
}

void
rsc::world::clear ()
{
  for (const auto &scene : m_scenes) {
    if (scene != nullptr) {
      scene->stop_and_clear ();
    }
  }
  m_scenes.clear ();
}

} // namespace wsl
