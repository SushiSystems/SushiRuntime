# examples

Programs written against the public API, one subject each. A program includes
`<SushiRuntime/sushiruntime.hpp>` and no header below it, and spells every name through
`using namespace SushiRuntime;`. The exceptions: `distributed_worker.cpp` includes only
`<SushiRuntime/distributed/distributed.hpp>`, `distributed_demo.cpp` includes both, and of the
three files in `adapter/` one includes the library. Read `quickstart.cpp` first, then the file
for the subject at hand:

| File | Shows |
| --- | --- |
| `quickstart.cpp` | A runtime, a `State`, one update rule and a stepped `run()` |
| `vecadd_demo.cpp` | Buffers passed as `In` and `Out`, and automatic dependency ordering |
| `stencil_demo.cpp` | A 2-D grid with an interior-only (halo) update over an `Extent` |
| `coupled_fields_demo.cpp` | Two fields evolving together through `Coupled` (Jacobi) |
| `game_engine_demo.cpp` | An ECS-style frame loop built from the data flow, each system named with `name()` |
| `streaming_world_demo.cpp` | A `DynamicGraph` whose set of work changes between steps |
| `operating_path_demo.cpp` | `RuntimeConfig`, `advanced()`, `run()` with its error branch, a launch `sized()` per step and a `read_range()` readback |
| `errors_demo.cpp` | A `Result` tested and handled, a refused recording read with `refusal()`, `compile()` before any step, and `clear()` |
| `tasks_demo.cpp` | `add_task`, `add_host` and `add_untracked` beside kernels, nodes named through `NodeOptions`, and the `plan()` they compile to |
| `stepping_demo.cpp` | `before_each_step`, then one rule through `run(steps)`, `run_until`, `run_async` with its `RunHandle`, and `run_for` |
| `data_access_demo.cpp` | The three residencies, `write_range()` and `read_range()` tested as results, an aligned buffer, `Runtime::wait()` and `advanced().queue()` |
| `extension_demo.cpp` | A domain library that records through `extend()` and the `Recorder`: `rule()`, `scratch()`, `kernel()` and `task()` |
| `host_hooks_demo.cpp` | What a host sets once per process: `version_matches()`, `set_assertion_handler()` and the log sink, installed around a run and restored |
| `adapter/` | A host program that reaches the library through one file, in its own types and its own status |
| `distributed_demo.cpp` | A master that offloads a task to a worker, or runs it itself (opt-in) |
| `distributed_worker.cpp` | The worker process of the distributed demo (opt-in) |

The examples read a result with `value()` where a failure is not their subject; `value()` stops
the process with the error's message. `errors_demo.cpp` is the one about failures.
`operating_path_demo.cpp` tests the result of each run and returns 1 with the message, and
`distributed_demo.cpp` does the same when the secret cannot be read or the master does not
start. `distributed_worker.cpp` returns the exit code of `run_worker()`.

`distributed_kernels.hpp` is the header the last two share; they also include
`<SushiRuntime/distributed/distributed.hpp>`. The distributed programs are built only when the
distributed layer is enabled (`sr build --distributed`).

A program returns 0 when its own check holds and 1 when it does not. The check compares what
the run wrote with a value computed on the host, and the last line printed is
`result: CORRECT` or `result: WRONG`. A number that depends on the machine, a duration or a
count of steps in a time budget, is labelled as such in the output and is not compared. A
target is named after its file: `sr run sr_quickstart`, `sr run sr_adapter_demo`.

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

The field starts with one unit of heat in the middle cell, and both ends reflect, so the total
stays 1. The program prints the step count, the duration of the run, which is labelled as
timing, and the total, and it returns 1 when the total has left 1 by more than 0.001.

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

`advance_on_host()` applies the same stencil to a copy of the plate on the host, and the program
compares every cell of the `State` with it, within 0.0001. It prints the total heat of both.
The block starts with 400 units, and the totals end a little below that, because the border
is held cold and takes the heat that reaches it.

## coupled_fields_demo.cpp

Two fields evolve together, one reading the other. `v` counts up by one each step and `u`
accumulates `v`. The coupled rule, `add_coupled(Coupled(out, in), kernel)`, reads the previous step's data for
every input, a Jacobi update: each `State` advances only after the whole step's kernels have
run, so writing one field never changes what another reads in the same step. That makes the
result independent of the order in which the two rules were added. The `v` that the `u` rule
reads is the previous step's `v`, not the value the `v` rule produces this step.

With `v` starting at 0 and incrementing each step, after STEPS steps `v == STEPS` and
`u == 0 + 1 + ... + (STEPS-1) == STEPS*(STEPS-1)/2`.

## game_engine_demo.cpp

A game-engine frame loop of four systems over 4096 entities, run for 600 frames.

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

`K_CULL_X`, the cull threshold, is 30.37 so that it sits off the grid. An entity's x is
`vx * 0.01 * step`, and no entity lands on 30.37 at an integer step. The crossing frame is
therefore identical on host and device, and the position frozen at the cull never differs by a
whole step of motion. Final x is `vx * (K_STEPS * K_TICK) = vx * 6`, one of 12, 24, 36, 48, 60 and
72; the first two stay under `K_CULL_X` and survive, and the rest are culled.

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
settings, asks what was built, handles a failed run and reads device memory back.

`Runtime::create()` takes one `RuntimeConfig`; here it asks for two workers with
`PinPolicy::Float`, which leaves the other cores to the host program. `advanced()` returns the
read-only view, from which the program prints each device by index and the worker count as
built. Those lines depend on the machine, and the output says so.

The buffer is allocated with `Residency::Device`, so the host cannot index it. `write_range()`
seeds it and `read_range()` copies single elements back.

One rule increments the first `live_count` elements. The count reaches the rule through
`sized(...)`, which the runtime polls before each step, so the second run covers twice
as many elements as the first and `compile_count()` stays at 1. After two runs of 8 steps the
elements read back are 16, 8 and 0: counted in both runs, in the second only, and in neither.

Both runs use `run()`, which returns the report or an `Error`. The program then runs an empty
graph on purpose, checks that the refusal carries `ErrorCode::InvalidGraph` and prints the remedy its
`switch` over `ErrorCode` picks for that code. `Runtime::create()` and the range calls return a
`Result` too; the program reads them with `value()`, which stops the process on an error.

## errors_demo.cpp

The three ways a call fails, each handled the way a program would handle it.

A cause outside the program comes back as an `Error` in a `Result`. The program tests
`Runtime::create()` and leaves `value()` alone. It then asks for a `State` whose bytes fill half
the address space. No device grants that, the result holds `AllocationFailed`, and the program
asks for 64 cells.

A recording the graph cannot keep is refused. `add_rule()` with a launch of 65 elements over
the 64 cells of the `State` records nothing, so `size()` stays 0, and the graph keeps the
`Error`. `refusal()` returns it at once. `compile()` returns the same error and builds no plan,
and `run()` returns it and runs no step. The first refusal stays until `clear()`.

`Graph::clear()` forgets the rules and the refusal. The program records the rule over 64 cells,
`compile()` succeeds before any step has run, and `run(10)` uses the plan it built, so
`compile_count()` is 1.

A recorder refuses these, and the next run returns the kept error:

| Recording | Code |
| --- | --- |
| A rule over a `State` that owns no memory | `InvalidAccess` |
| A launch that reaches past the `State` it steps or reads | `InvalidGraph` |
| A coupled rule whose input `State` is smaller than the one it writes | `InvalidGraph` |
| More parameters than `NodeOptions::K_MAX_PARAMETERS` | `InvalidGraph` |
| A late-bound node whose fault record cannot be allocated | `AllocationFailed` |

A rule the caller broke is the third kind and has no line here, because it stops the process:
a device index the runtime does not have, a window past the end of a buffer, `value()` on an
error. `host_hooks_demo.cpp` installs the handler such a stop calls.

```
errors:
  state(too many cells) -> AllocationFailed, so the program asks for 64 cells
  refusal() -> InvalidGraph: add_rule(launch, state, kernel), rule 0: the count 65 reaches 65 elements and the State holds 64; shrink the launch or allocate the handle larger.
  compile() and run() return that error: yes (rules recorded = 0)
  after clear(): 10 steps, cell = 10, compiles = 1
  result: CORRECT
```

## tasks_demo.cpp

The three verbs for work that is not one kernel over named buffers, and the plan the data
they name implies. Five nodes are recorded:

| Node | Verb | Reads | Writes |
| --- | --- | --- | --- |
| `fill` | `add` with `Out` | | `values` |
| `double_then_add_one` | `add_task` | `values` | `staged`, `result` |
| `add_three` | `add` with `InOut` | `result` | `result` |
| `checksum` | `add_host` | `result` | `checksum` |
| `apart` | `add_untracked` | | |

The body of `add_task` receives the `sycl::queue` of its device and the events it must run
after. It submits two command groups, makes the second depend on the first with
`depends_on()`, waits for nothing and returns the event of the second; the runtime releases
`add_three` when that event completes. The body of `add_host` takes no argument and runs on a
worker thread once `add_three` has finished, so it reads `result` through the pointer it
captured. `add_untracked` names no data: its kernel writes a buffer no other node touches, and
nothing orders it.

Each node states its name in a `NodeOptions`, the last argument of every verb. `named()` builds
one by assigning `name` to a default object, which is how a caller states one field alone.

`Graph::plan()` compiles the graph, runs nothing and returns a `PlanView`: one `PlanNode` per
node, in recording order, with the position of its rule, its name and how many nodes it waits
for. The program prints the plan before it runs and compares the five counts:

```
tasks: 5 rules, 5 nodes
  rule 0  fill                 waits on 0
  rule 1  double_then_add_one  waits on 1
  rule 2  add_three            waits on 1
  rule 3  checksum             waits on 1
  rule 4  apart                waits on 0
  checksum = 1051648  (expected 1051648)
  result: CORRECT
```

After the run, element i of `result` is 2 * i + 4 and `checksum` is their sum over 1024
elements. `compile_count()` is 1: the run used the plan `plan()` built.

## stepping_demo.cpp

One rule driven by each run entry. The rule adds a word, `gain`, to every cell of a `State`.
A hook added with `before_each_step()` writes that word before each step: 1 plus the steps the
run in progress has finished. The hook also adds the word to a count on the host, which is
what the field must hold at the end. The graph compiles once for all four entries.

| Entry | Steps here | Stops when |
| --- | --- | --- |
| `run(4)` | 4 | The count is reached |
| `run_until(done)` | 5 | `done()` returns true; it is called before each step and reads the field |
| `run_async()` | 1 | The step is on the worker pool; `wait()` on the `RunHandle` ends it |
| `run_for(budget)` | At least 1 | The wall-clock budget, 2 ms, has elapsed |

`run_async()` returns a `Result` that holds the handle. The program tests it, because an engine
that cannot start a step without blocking answers `Unsupported`. `pending()` is true until
`wait()` returns the report, and the `State` advances in `wait()`.

Every entry returns a `RunReport`. The program prints three of its fields:
`total_tasks_executed`, one per node per step; `is_successful`; and `cancelled`, which
`cancel()` would set.

```
stepping: 256 cells; a step adds 1 + the steps its run finished
  run(4)     tasks = 4, successful = yes, cancelled = no, cell = 10
  run_until  tasks = 5, successful = yes, cancelled = no, cell = 25
  run_async  tasks = 1, successful = yes, cancelled = no, cell = 26
  run_for    <count> steps in a budget of 2 ms (depends on the machine)
  the field matches the host count: yes (compiles = 1)
  result: CORRECT
```

The first three cells are fixed: 1 + 2 + 3 + 4 is 10, five more steps reach the threshold of
25, and one step adds 1. The step count of `run_for()` differs from machine to machine, so the
check compares every cell with the host count and not with a constant.

## data_access_demo.cpp

How a host program gets data into a buffer and out of it, for each place a buffer can live.

| Buffer | Residency | The host reaches it by |
| --- | --- | --- |
| `doubled` | `Residency::Shared`, the default | Indexing it once the run has returned |
| `resident` | `Residency::Device` | `write_range()` and `read_range()`, or a copy on the device's queue |
| `first` | `Residency::Host` | Indexing it; a kernel writes it across the bus |

`resident` is allocated with the fourth argument of `Runtime::buffer()`, an alignment of 64
bytes, and the program checks the address. `is_host_accessible()` answers whether the host may
index a buffer, and `residency_name()` spells the residency.

`write_range()` seeds `resident` and returns a `Result<void>`; `read_range()` returns the last
four elements as a `Result` that holds a vector. The program tests both and prints the error of
one that failed. Both wait for their copy, and `advanced()` counts the bytes they moved.

`advanced().queue()` returns the `sycl::queue` the runtime submits a device's work on. The
program submits a kernel of its own there, which adds 1 to every element, and does not wait
for its event. That work is outside every graph and no rule is ordered against it, so the
program calls `Runtime::wait()`, which returns when every device queue is idle, before the
graph that reads `resident` runs. After the run it copies the whole buffer back with a `memcpy`
on the same queue and waits for that one itself.

```
data access: 4096 floats
  doubled  Shared host-addressable: yes
  resident Device host-addressable: no
  first    Host   host-addressable: yes
  resident is aligned to 64 bytes: yes
  first[0] = 1, doubled[4095] = 8192
  range calls moved 16384 bytes to the device and 16 back
  result: CORRECT
```

The check compares every element of the copy with i + 1, every element of `doubled` with twice
that, the four elements `read_range()` returned and the two byte counts.

## extension_demo.cpp

For the author of a library on top of the runtime. `ScaleLibrary::record_scale_offset()` is
such a library's whole interface to it: a free function that takes a `Recorder&` and the
buffers of one operation, records the nodes of that operation and returns the position of
their rule. The library names no `Graph`, so the same function records into a region of a
`DynamicGraph`.

The operation is out = in * factor + offset, in two nodes with a staging buffer between them:

| Recorder verb | What the library uses it for |
| --- | --- |
| `scratch()` | The staging memory; the recorder owns it and frees it with the nodes |
| `rule()` | A scope that makes both nodes one rule, under one registration index |
| `kernel()` | The first node: a launch over a count that reads `in` and writes the stage |
| `task()` | The second node: a body that takes the queue, reads the stage and writes `out` |

The stage is not a `Buffer`, so the library names it as a `BufferRegion` over the pointer
`scratch()` returned, and `Access(Reads(...), Writes(...))` orders the two nodes by it.

The program reaches the function through `extend()`, which hands it the graph's recorder in the
middle of a chain, and reads the same position from `last_rule()`. `name()` after `extend()`
names the rule. The second node states a name of its own in its `NodeOptions` and keeps it, and
the first takes the rule's:

```
extension: 2 rules, 3 nodes, the library recorded rule 1
  rule 0  fill           waits on 0
  rule 1  scale_offset   waits on 1
  rule 1  add_offset     waits on 1
  output[1023] = 3076  (expected 3076)
  result: CORRECT
```

Once the graph has compiled, `rule_registration()` of that position returns the index the
rule's node events carry in a profiled `RunReport`; before that it returns
`K_NO_REGISTRATION`. The check compares every element of the output with i * 3 + 7.

## host_hooks_demo.cpp

The three things a host program sets once, before it creates a runtime. All are state of the
process and not of one runtime, so the program restores each when it is done.

`version_matches()` compares the release of the headers with the release of the library that
was loaded. `Runtime::create()` stops the process when they differ, so a host that wants to
report the mismatch itself asks first.

`set_assertion_handler()` installs the function a broken rule calls before the process stops,
and returns the one it replaces. The handler here writes the message, the file and the line in
the host's own format. A handler that returns does not keep the process alive, so the program
breaks no rule: it installs the handler, runs, puts the old one back and checks that the
handler it took out was its own.

`SushiLog::set_log_sink()` routes every log record of the library to a callable of the host, which
receives each `LogRecord` with its fields apart.
`SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info)` admits the lifecycle lines a runtime writes while it starts and
stops, and `SushiLog::set_log_synchronous(true)` delivers each record before the call that logged it
returns, so the sink has seen every line when the runtime is gone. The sink counts lines. An
empty sink restores the default one, which writes to standard error.

```
host hooks: headers and library are of one release
  the sink received the runtime's log lines: yes
  the assertion handler was the host's until restored: yes
  values[1023] = 2046  (expected 2046)
  result: CORRECT
```

The run between the two installs fills a buffer with twice its index. The check compares its
last element, that the sink received at least one line, and the handler that was restored.

## adapter/

A host program that needs one thing from a compute library: a field that advances by a rule
for a number of steps and can be read back. It gets it through three files, built into one
program, `sr_adapter_demo`.

| File | Holds | Includes the library |
| --- | --- | --- |
| `adapter/diffusion_backend.hpp` | The host's side of the seam: `StatusCode`, `Status`, the handle of a backend it cannot look into, and four functions | No |
| `adapter/sushiruntime_adapter.cpp` | The four functions on SushiRuntime, and the mapping of every `ErrorCode` to a `StatusCode` | Yes |
| `adapter/adapter_demo.cpp` | The host: it seeds a field, advances it and compares it with its own computation | No |

The header declares `DiffusionBackend` and does not define it, so the host holds a
`BackendHandle` and passes it back to `create_backend()`, `allocate_field()`, `advance()` and
`read_field()`. Each returns a `Status`. The adapter defines the type: a `Runtime`, a
`State<float>` and a `Graph` with the one rule that steps the field.

`status_of()` is the one place an `Error` becomes a `Status`. Its `switch` names every
`ErrorCode` and has no `default`, so a compiler that warns on an unhandled enumerator reports a
code a release added. A second `status_of()` takes any `Result` and returns `Ok` or the mapped
error, and every call into the library ends in one of the two.

A rule the caller broke stops the process and returns nothing to map, so the adapter tests what
it can before the library sees it: `create_backend()` asks `version_matches()` before
`Runtime::create()`, and `allocate_field()` turns down a field of no cells. What the library
refuses and keeps, it reads back: after `add_rule()` it returns the graph's `refusal()` when
there is one.

The host shows the failure path once. It calls `advance()` before a field exists, the graph has
no rule, the library returns `InvalidGraph`, and the host prints its own code:

```
adapter: advance before allocate_field -> InvalidRequest: Graph has no rule to run.
adapter: 256 cells, 100 steps, total heat = 1.000
  result: CORRECT
```

The check compares every cell with the same diffusion advanced on the host, within 0.00001.

When a release of SushiRuntime changes a name or a signature, `sushiruntime_adapter.cpp` is the
file that changes. The header and the host do not, because neither names the library.

## distributed_demo.cpp

The master of the distributed demo. It attaches a master to a runtime with
`Distributed::Cluster::attach` and a `ClusterOptions`, listens on TCP, waits for one worker, adds
two vectors and checks every element of the sum. The add is one kernel whose `NodeOptions`
name the operation `demo.vector_add`. With a worker registered the node runs there: the worker
pulls the two inputs and streams the sum back. Without one, the kernel given to `add()` runs it
on this machine. The
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
`Cluster::share()`, which needs the handle `attach()` returned. The default offload policy also keeps a task local when its
inputs are under 64 KiB, so a run with `--elements` below 8192 reports `declined=1` even with a
worker connected.

## distributed_worker.cpp

The worker of the distributed demo. Its `main` builds a `KernelRegistry`, registers the demo's
kernels and returns `Distributed::run_worker(argc, argv, kernels)`. The library does the rest:
the flags, the secret, the connection with reconnect, the signal handlers and the exit code. The
flags and exit codes are listed in
[the module README](../../modules/extension/distributed/README.md), section "worker_main.hpp". Stop a worker with Ctrl-C; it
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
