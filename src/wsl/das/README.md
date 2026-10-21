# `wsl::das` — daScript Integration

Embeds the daScript scripting language into the Weasel engine, allowing gameplay logic (components, systems) to be written in `.das` files.

## Execution Paths

The engine supports Daslang-authored gameplay components and systems across
interpretation and AOT execution. Engine built-in components remain C++/EnTT
types; user runtime C++ components and systems are not supported.

```
                           Daslang source
                           ┌──────┴──────┐
                           │             │
                    interpreted       AOT-generated C++
                     editor/runtime    CMake build
                           │             │
                           └──────┬──────┘
                                  v
                           same EnTT runtime
```

### Path 1: Daslang Interpretation (Editor)

Used when the editor loads a project with `.das` source files.

1. `das_engine::initialize()` sets up `ProjectFsFileAccess` and registers builtins.
2. `addFsRoot("components", comp_dir)` allows `require mouse_rotate` to find `mouse_rotate.das` in any project directory.
3. For each `.das` file:
   - `compileDaScript(path)` compiles to a `Program`.
   - A new `Context` is created per program (avoids shared-node corruption).
   - `program->simulate(ctx)` generates sim nodes.
   - Functions are merged into `file_fn_lookup[path]`.
   - `get_struct_info()` extracts field metadata for component registration.

**Critical**: Each compiled program gets its own `Context`. Two programs sharing a Context would double-relocate sim nodes, causing `prefix->magic==0xdeadc0de` assertion failures.

### Path 2: Daslang AOT (CMake Build)

Used when building a standalone release binary via CMake.

1. `CMakeLists.txt` calls `weasel_aot_das()` (from `cmake/WeaselDasAOT.cmake`).
2. For each `.das` file, `weasel-cli aot` compiles in-process with the real
   `Module_WeaselApi` loaded. No fake `weasel_api.das` stub is used.
3. Generated `.cpp` files are linked into the final executable.

The generated C++ links against the same engine binding types and functions
used by the interpreted path. The original `.das` sources remain available to
the current runtime loader for program metadata and class-adapter execution.

## Module System

### Module Files

| File | Location | Role |
|------|----------|------|
| `weasel.das` | `modules/` | Gameplay facade: `GameSystem` base, `World`/`Entity`/`SystemContext`, typed component API, `query` macros, engine services. Includes the `EcsSystem` adapter base (folded in; the old `weasel_ecs.das` is gone) |
| `weasel_editor.das` | `modules/` | Editor-only viewport helpers; gameplay code must never require it |

Gameplay scripts import exactly one module: `require weasel`.

### Module Resolution

When a `.das` file does `require weasel`, the resolution depends on the execution path:

| Path | Resolution | Typed helpers visible? |
|------|-----------|------------------------|
| daScript Interpretation (Editor) | `weasel.das` gameplay facade over C++ `Module_WeaselApi` (real bindings) | Yes — world-scoped `World`/`Entity` API, `query` macros, services |
| daScript AOT (CMake Build) | Same facade compiled in the in-process compiler | Yes — same facade (module-less scripts; explicit-`module` scripts hit an upstream AOT crash) |

The key insight: gameplay code never touches the native module directly. `weasel.das` is the single public surface; the legacy helper modules (`weasel_helpers.das`, `weasel_ecs.das`) were deleted after the facade absorbed them.

```
require weasel      → weasel.das facade (interpreted and AOT)
```

> ⚠️ **Ignore any `weasel_api.das` you find on disk.** `weasel_api` is a C++
> module (`Module_WeaselApi`); there is no `.das` declaration for it. Old
> installs shipped a hand-written `weasel_api.das` stub that declared
> functions which do not exist (`set_position`, `get_position_x`, camera
> helpers, …) and would shadow the real bindings. Current builds remove that
> stub at install time. For the authoritative, complete list of available
> functions, use the MCP tool `describe_das_api` or the CLI command
> `das api` (see Workstream B).

### ProjectFsFileAccess

The `ProjectFsFileAccess` class (defined in `das_engine.cpp`) extends `FsFileAccess` to search project source directories when resolving bare `require` names. Without this, daScript's built-in resolution only checks the same directory as the requiring file.

The engine registers the following roots:
- **daslib**: Built-in daScript modules (`math`, `strings`, etc.)
- **engine_modules**: `src/wsl/das/modules/` — for `require weasel`
- **components**, **systems**: Project-specific directories (added via `addFsRoot()`)

## Key Files

| File | Description |
|------|-------------|
| `das_engine.hpp/cpp` | `das_engine` class — compiles and executes `.das` files, manages contexts, provides SIGSEGV protection |
| `wsl_api_module.hpp/cpp` | `Module_WeaselApi` — registers C++ bindings (entity ops, transforms, scene queries, component access) with daScript |
| `das_ecs_binds.hpp/cpp` | Native ECS module support used by the Daslang runtime |
| `das_system_adapter.hpp/cpp` | `das_system_adapter` — dual-inheritance bridge: `ecs_system` (C++) + `EcsSystemAdapter` (daScript class adapter pattern) |
| `das_system_adapter.hpp/cpp` | Daslang class-adapter system bridge |
| `das_registration.hpp/cpp` | File discovery and registration — compiles `.das` files, extracts struct info, registers components/systems |
| `das_interop.hpp/cpp` | Low-level `addInterop` functions for runtime type introspection (`describe_type`, `type_name`, etc.) |
| `modules/weasel.das` | Gameplay facade: typed world/entity API, `query` macros, engine services. Includes the `EcsSystem` adapter base and `GameSystem` |

## Writing a daScript System

Create a `.das` file in your project's `systems/` directory:

```das
options gen2
module my_system

require weasel

class System : GameSystem {
    def override on_start(var ctx : SystemContext) : void {
        ctx.input.bind_key("jump", 44)  // Space scancode
    }

    def override update(var ctx : SystemContext; dt : float) : void {
        if ctx.input.just_pressed("jump") {
            query(ctx.world) $(t : Transform&; mr : MouseRotate&) {
                t.position.y += 1.0
            }
        }
    }

    def override on_stop(var ctx : SystemContext) : void {
        // cleanup
    }
}
```

The engine instantiates the class literally named `System`; it must
extend `GameSystem`. Lifecycle hooks take a mutable `var ctx` so service
methods (`ctx.input.*`, `ctx.log.*`) bind. Input is pull-based: bind
named actions once, read edges/held state each frame.

### Lifecycle Methods

| Method | When called |
|--------|------------|
| `on_start(ctx)` | Once after the system is loaded and all entities are available |
| `update(ctx, dt)` | Every frame |
| `fixed_update(ctx, fixed_dt)` | Zero or more times per frame, before the variable pass, with the physics fixed step |
| `late_update(ctx, dt)` | Once per frame after the variable pass |
| `on_stop(ctx)` | When the system is being shut down or the scene is changing |

### Component Access and Queries

The `weasel` facade provides typed world-scoped access. Presence is
explicit per call (`has` / `get` which panics when absent /
`get_or_default` value copy / `try_get` conditional fill):

```das
var world = current_world()
var e = world.create_entity()
assert(has(world, e, Transform()))
var t = get(world, e, Transform())
t.position.x = 10.0

query(world) $(t : Transform&; mr : MouseRotate&) {
    t.position.x += 1.0
    mr.yaw = 5.0
}
```

Built-in views are live proxies into EnTT storage. Daslang component
query references point directly into the scene-local Daslang component
pool; there is no write-back step. Structural changes inside a query are
rejected — queue them through `ctx.commands` instead.

## SIGSEGV Protection

The `call_void_function_safe()` methods recover from daScript panics and report them as errors. Native faults such as null dereferences, out-of-bounds accesses, and use-after-free crashes are fatal and are not converted into a recoverable VM error.

The crash handler uses `sigaltstack` to print a short diagnostic before restoring the default SIGSEGV action and re-raising the signal. daScript `panic()` recovery remains a separate path and restores engine bookkeeping after the VM's `longjmp`.

## Architecture Decisions

### Why Per-Program Contexts?

Each compiled program gets its own `Context` because `Context::relocateCode` relocates ALL sim nodes including shared module nodes. If two programs that share modules (via `require`) are simulated into the same Context, the second program's `relocateCode` tries to copy already-relocated shared nodes, causing the `prefix->magic==0xdeadc0de` assertion failure in `SimNode::copyNode`.

### Why Not `require_dynamic_modules`?

The engine intentionally does NOT call `require_dynamic_modules` during initialization. It compiles `.das_module` files and stores them in thread-local `ModuleKarma`. When the worker thread later compiles user `.das` files, the compiled modules may be in an inconsistent state across threads, causing corruption (hash set corruption in `buildAccessFlags`, etc.). Instead, modules like `builtin.das` are compiled on-demand by `compileDaScript` on the worker thread.

### Native module plus facade

`weasel_api` is the real native module in both interpreted and in-process AOT
compilation. `weasel.das` is the single gameplay surface over it: typed
`World`/`Entity` handles, the `query` AST macros, and the engine services.
The legacy helper modules are deleted; nothing outside `weasel.das` may
define a parallel component-access or query surface.
