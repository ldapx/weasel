# AGENTS.md

Guidance for AI agents working in this repository.

## What this is

A showcase project for the Weasel Engine Daslang API (`require weasel`):
five small game systems demonstrating animation state, audio controls,
trigger-volume subscriptions, UI callbacks, and scene-transition
degradation. The systems are exercised headless by
`tests/weasel-das/test_component_accessors.cpp` ("showcase example
project runs headless"), which compiles the real files and runs their
lifecycle — keep them compiling and side-effect honest.

## Project layout

- `wslpro.json` - project manifest (paths, default scene)
- `rsc/scenes/` - scenes stored as `.wscn` files
- `src/systems/` - runtime code (`*_demo_system.das`)
- `src/main.cpp` - standalone game entry point

The `wslpro.json` manifest must include the `window` block (see any
CLI-generated project); without it manifest parsing fails and the
project does not load.

## Making engine calls

Prefer attaching to a running Weasel editor instance:

    weasel-cli -a ent new Player
    weasel-cli -a comp set 0 transform position '[0, 2, 0]'

`-a` attaches to the first running editor and uses its open
project. Without an editor it warns and falls back to standalone
execution (add `--project <path>` to select one). Mutations
auto-save when running standalone; when attached, saving is done
in the editor.

For interactive exploration use the REPL: `weasel-cli -i -a`

The same functionality is also exposed as MCP tools through
`weasel-mcp-server`. When connected to it, prefer those tools
(list_commands, describe_command, list_components,
describe_component, describe_namespace) over shelling out.

## Notes

- Core systems (Transform, Physics, 3D Render, ...) exist in
  every scene automatically; do not create them manually.
- Validate structural changes with:
  `weasel-cli validate-project wslpro.json`
- Validate scenes with:
  `weasel-cli validate-scene rsc/scenes/showcase.wscn wslpro.json`
- Daslang gotchas that have bitten here: `assert` bodies must be pure
  (hoist native calls), `ctx` parameters must be `var` for service
  methods, and struct methods cannot overload by arity.
