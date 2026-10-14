# engine_handles

The module translates between the opaque names the public surface holds and the engine types behind
them: a `Detail::Plan` is a compiled graph, and a `Detail::LateControls` is a node's late bindings.
It is an internal module: its headers reach the modules that name it and the tests, and are never
installed. It is header-only, and its names are in `SushiRuntime::Detail`. It declares the two
surface names itself, so it includes no header of `api`.

It depends on `task` and `graph`.

## Public entry points

| Header | Holds |
| --- | --- |
| `plan_access.hpp` | `engine_plan()`, `surface_plan()` |
| `late_controls_access.hpp` | `engine_controls()`, `surface_controls()` |
