# examples

Nine programs written against the public API. Eight include `<SushiRuntime/sushiruntime.hpp>`;
`distributed_worker.cpp` includes only `<SushiRuntime/distributed/distributed.hpp>`, and
`distributed_demo.cpp` includes both. Each spells every name through
`using namespace SushiRuntime;`. Read `quickstart.cpp` first, then the focused demos:

| File | Shows |
| --- | --- |
| `quickstart.cpp` | A runtime, a `State`, one update rule and a stepped `run()` |
| `vecadd_demo.cpp` | Buffers passed as `In` and `Out`, and automatic dependency ordering |
| `stencil_demo.cpp` | A 2-D grid with an interior-only (halo) update over an `Extent` |
| `coupled_fields_demo.cpp` | Two fields evolving together through `Coupled` (Jacobi) |
| `game_engine_demo.cpp` | An ECS-style frame loop built from the data flow, each system named with `name()` |
| `streaming_world_demo.cpp` | A `DynamicGraph` whose set of work changes between steps |
| `operating_path_demo.cpp` | `RuntimeConfig`, `advanced()`, `try_run()` with its error branch, `Dynamic` and a `read_range()` readback |
| `distributed_demo.cpp` | A master that offloads a task to a worker, or runs it itself (opt-in) |
| `distributed_worker.cpp` | The worker process of the distributed demo (opt-in) |

When a runtime call fails, every example except `distributed_worker.cpp` prints the library's message to `stderr` and exits with 1; the worker returns the exit code of `run_worker()` instead.

`distributed_kernels.hpp` is the header the last two share; they also include
`<SushiRuntime/distributed/distributed.hpp>`. The distributed programs are built only when the
distributed layer is enabled (`sr build --distributed`).

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
accumulates `v`. The coupled rule, `add(Coupled(out, in), fn)`, reads the previous step's data for
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
Work on entities that does not conflict runs in parallel. Each system is named with `name()`, so
a profile lists `gravity`, `integrate`, `collide` and `cull` instead of four unnamed tasks.

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

## operating_path_demo.cpp

The calls a program makes once it runs somewhere other than a developer's desk: it chooses its
settings, asks what was built, handles a failed run without an exception and reads device memory
back.

`Runtime::create()` takes one `RuntimeConfig`; here it asks for two workers with
`PinPolicy::Float`, which leaves the other cores to the host program. `advanced()` returns the
read-only view, from which the program prints each device by index and the worker count as built.

The buffer is allocated with `Residency::Device`, so the host cannot index it. `write_range()`
seeds it and `read_range()` copies single elements back.

One rule increments the first `live_count` elements. The count reaches the rule through
`Dynamic().sized(...)`, which the runtime polls before each step, so the second run covers twice
as many elements as the first and `compile_count()` stays at 1. After two runs of 8 steps the
elements read back are 16, 8 and 0: counted in both runs, in the second only, and in neither.

Both runs use `try_run()`, which returns the report or an `Error`. The program then runs an empty
graph on purpose, checks that the refusal carries `ErrorCode::InvalidGraph` and prints the remedy its
`switch` over `ErrorCode` picks for that code. An exception can still come from `Runtime::create()` and
from the range calls, and `main` catches `SushiException` for those.

## distributed_demo.cpp

The master of the distributed demo. It attaches a master to a runtime with
`Distributed::Cluster::attach` and a `ClusterOptions`, listens on TCP, waits for one worker, adds
two vectors and checks every element of the sum. The add is one task tagged with the operation
`demo.vector_add`. With a worker registered the task runs there: the worker pulls the two inputs
and streams the sum back. Without one, the lambda given to `add()` runs it on this machine. The
sum is the same either way, because both paths call `add_element()` of `distributed_kernels.hpp`.

The program prints where the task ran, which it reads from `Cluster::statistics()`:
`completed_remote` above 0 means a worker finished it. A run with a worker ends with these lines,
followed by one line of byte and chunk counters:

```
distributed_demo: N = 1048576, result CORRECT
  ran on: a worker
  offloaded=1 declined=0 redispatched=0 stale_writebacks=0
```

| Flag | Default | Meaning |
| --- | --- | --- |
| `--bind address` | `127.0.0.1` | Address to listen on |
| `--port n` | 5555 | Port to listen on; 0 takes any free port, which the first line prints |
| `--secret-file path` | none | The cluster's key, a file of at least 32 bytes |
| `--wait-s n` | 30 | Seconds to wait for a worker; 0 runs the task here at once |
| `--elements n` | 1048576 | Length of each vector |

A value follows its flag as the next argument or after `=`. The buffers are shared with
`Buffer::distributed()` after `attach()`; called before it, `distributed()` shares nothing, logs
one WARN and the task stays local. The default offload policy also keeps a task local when its
inputs are under 64 KiB, so a run with `--elements` below 8192 reports `declined=1` even with a
worker connected.

## distributed_worker.cpp

The worker of the distributed demo. Its `main` builds a `KernelRegistry`, registers the demo's
kernels and returns `Distributed::run_worker(argc, argv, kernels)`. The library does the rest:
the flags, the secret, the connection with reconnect, the signal handlers and the exit code. The
flags and exit codes are listed in
[the distributed guide](../../docs/guides/DISTRIBUTED_GUIDE.md), §5. Stop a worker with Ctrl-C; it
finishes the running kernel, says goodbye to the master and exits 0. `run_worker(argc, argv, ...)`
refuses a flag it does not know, so a worker program with flags of its own parses them, fills a
`WorkerOptions` and calls the overload that takes it.

### One machine

The master listens on loopback, so no secret is needed. Start the two in either order, each in
its own terminal: the worker keeps trying for 60 seconds and the master waits 30.

```
sr run sr_distributed_demo
sr run sr_distributed_worker -- --master 127.0.0.1:5555
```

### Two machines

A master that other machines can reach refuses to start without a secret. Make a key file of 32
random bytes once and copy it to both machines; it is read byte for byte, so both must hold the
same file.

```
head -c 32 /dev/urandom > cluster.key
```

On Windows, in PowerShell:

```
$key = New-Object byte[] 32
[Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($key)
[IO.File]::WriteAllBytes("$PWD\cluster.key", $key)
```

On the master, whose address is 192.168.1.10 in this example:

```
sr run sr_distributed_demo -- --bind 0.0.0.0 --port 5555 --secret-file cluster.key
```

On the worker:

```
sr run sr_distributed_worker -- --master 192.168.1.10:5555 --secret-file cluster.key
```

`sr run` passes what follows `--` to the program. A worker with the wrong key exits 3, and one
that cannot reach the master within 60 seconds exits 2. The two machines prove the key to each
other at the handshake; the frames after it are neither encrypted nor signed, so this is for a LAN
both machines trust.

## distributed_kernels.hpp

What the master and the worker must agree on, stated once: the operation id `K_VECTOR_ADD`
(written with the `_op` literal), the
kernel `add_vectors()` a worker runs for it, and `register_kernels()`, which puts every kernel of
the demo into a registry. A kernel receives its buffers as `BufferView`s, an address and a size
each, so `add_vectors()` takes the element count from the size of its output. A new operation is
added here and nowhere else.
