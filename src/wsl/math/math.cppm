module;
// Global module fragment: everything the exported engine headers skip under
// WSL_MODULE_BUILD is provided textually here. Math depends on glm (values),
// entt + component_meta (inline register_meta) and the STL — all
// global-module attached, so legacy TUs and module consumers share the same
// entities. The ImGui inspector widgets live in editor_support, not here.
#include <entt/entt.hpp>
#include "../comp/component_meta.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#define WSL_MODULE_BUILD

export module wsl.math;

export {
#include "vector.hpp"
#include "matrix.hpp"
}
