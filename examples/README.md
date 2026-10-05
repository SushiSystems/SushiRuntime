# examples

Seven programs written against the public API. Read `quickstart.cpp` first, then the focused
demos:

| File | Shows |
| --- | --- |
| `vecadd_demo.cpp` | Buffers and automatic dependency ordering |
| `stencil_demo.cpp` | A 2-D grid with an interior-only (halo) update |
| `coupled_fields_demo.cpp` | Several fields evolving together (Jacobi) |
| `game_engine_demo.cpp` | An ECS-style frame loop built from the data flow |
| `streaming_world_demo.cpp` | A graph whose set of work changes between steps |
| `distributed_demo.cpp` | Offloading a task to a remote worker (opt-in) |

## quickstart.cpp

The shortest complete SushiRuntime program: one include, a runtime, a typed `State`, a single
update rule and a stepped run. It evolves a 1-D field with a three-point diffusion stencil.
The runtime double-buffers the `State`, hands the current and next pointers to the rule,
compiles the graph once and replays it for every step. The program holds no `sycl::handler`,
no manual buffer swap and no hand-wired dependency.

A `State` is a field that evolves in time. The runtime keeps two buffers and swaps them each
step, so a rule reads the current field and writes the next. The rule receives the index and
the two buffers; the runtime supplies the pointers and tracks the read and the write.
`run(STEPS)` compiles once and replays, and the worker pool, device dispatch and buffer
swapping need no code from the caller.

## vecadd_demo.cpp

The "hello, dependencies" example: C = A + B over typed buffers, with the two tasks ordered by
the data they touch.

Three buffers are allocated as `Buffer<float>` handles. The first task fills A and B, which it
writes. The second reads A and B and writes C. Because the add task reads what the fill task
wrote, the runtime infers a read-after-write edge and runs them in order; no dependency is
wired by hand, and only this true dependency is serialized. `In(...)` and `Out(...)` wrap a
buffer so that `add()` registers the access for the tracker and hands the kernel the raw
element pointer as an argument. The kernel writes through that pointer directly.

Shared buffers are host-addressable, so the host indexes them directly once the run has
completed.

## stencil_demo.cpp

A 2-D heat-diffusion stencil written against the fluent API: a double-buffered `State`, a
multidimensional `Extent` that updates only the interior, and a stepped run. The fixed
Dirichlet border is the halo the extent skips. The program holds no raw `sycl::handler` and no
manual ping-pong.

Both buffers of the `State` are seeded identically, a cold plate with a hot interior block.
The interior-only update never writes the border, so the border must already hold its fixed
boundary value in each buffer.

The extent covers the (W-2) x (H-2) interior. The one-cell border stays fixed, and the kernel
receives the global 2-D index already offset to (1,1).

## coupled_fields_demo.cpp

Two fields evolve together, one reading the other. `v` counts up by one each step and `u`
accumulates `v`. The coupled `add(out, in, fn)` overload reads the previous step's data for
every input, a Jacobi update: each `State` advances only after the whole step's kernels have
run, so writing one field never changes what another reads in the same step. That makes the
result independent of the order in which the two rules were added. The `v` that the `u` rule
reads is the previous step's `v`, not the value the `v` rule produces this step.

With `v` starting at 0 and incrementing each step, after STEPS steps `v == STEPS` and
`u == 0 + 1 + ... + (STEPS-1) == STEPS*(STEPS-1)/2`.

## game_engine_demo.cpp

A small game-engine frame loop run end to end on the runtime through the public fluent API. It
shows the task graph behaving like a game engine.

Components are component arrays, one `Buffer` per field: `px`, `py`, `vx`, `vy`, `alive`.

Systems are graph nodes that declare which components they read and write with `In`, `Out` and
`InOut`. The dependency tracker orders them, and no schedule is written by hand: gravity
writes velocity, integrate reads it, collision rewrites it, culling reads the new position.
Work on entities that does not conflict runs in parallel.

The frame is compiled once and replayed every step by `run(STEPS)`, with no rebuild per frame.

Despawn is deferred. The cull system writes `alive` only after every physics system has read
it that step, a write-after-read edge, so a kill takes effect on the next frame and never in
the middle of one. This is the command-buffer discipline an ECS needs.

The result is checked against an independent scalar reference that runs the same four systems
in the same order.

`XMAX`, the cull threshold, is 30.37 so that it sits off the grid. An entity's x is
`vx * 0.01 * step`, and no entity lands on 30.37 at an integer step. The crossing frame is
therefore identical on host and device, and the position frozen at the cull never differs by a
whole step of motion. Final x is `vx * (STEPS * DT) = vx * 6`, one of 12, 24, 36, 48, 60 and
72; the first two stay under `XMAX` and survive, and the rest are culled.

## streaming_world_demo.cpp

A graph whose set of work changes between steps, the open-world streaming case.
`rt.dynamic_graph()` partitions work into regions keyed by a caller id, here a tile index.
`region(k)` records tasks through the same `add()` surface as a static graph, and `drop(k)`
removes a region. A mutation takes effect at the next step boundary, and only the regions that
changed are rebuilt. An unchanged tile is never recompiled.

The world holds a grid of tiles with a window of active ones. The window is tiles {0,1,2,3}
first and then slides to {2,3,4,5}: tiles 0 and 1 stream out, 4 and 5 stream in, and 2 and 3
stay live across the recomposition without being rebuilt. Each active tile's task increments
every cell once per step, so the final cell value is the number of steps that tile was in
view: STEPS for tiles 0, 1, 4 and 5, 2 * STEPS for tiles 2 and 3, and 0 for tiles 6 and 7.

## distributed_demo.cpp

The master and worker entry for the distributed layer. One binary plays either role, selected
by `--distributed=master|worker`. Workers connect over TCP and run a registered `vec.add`
kernel; the master offloads a vector add and checks the result.

```
sr run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555
sr run sr_distributed_demo --distributed=master --port=5555 --workers=1
```
