// Animator meta registration.
//
// The ImGui inspector widget (custom_inspect) lives in editor_support; this
// translation unit registers the plain data fields so the component stays
// introspectable by tooling (MCP describe_component, generic editors).

#include "animator.hpp"

namespace wsl
{

namespace comp
{

void
animator::register_meta ()
{
  register_meta_impl ();
}

} // namespace comp

} // namespace wsl
