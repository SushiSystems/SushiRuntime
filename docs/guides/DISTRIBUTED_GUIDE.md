# Distributed execution guide

SushiRuntime can offload individual tasks from one machine (the **master**) to
others (the **workers**) over a thin TCP layer — **no MPI**. This guide explains
when to use it, how to turn it on, how to run it, and exactly what travels over
the wire so you can reason about correctness and performance.

This layer is **opt-in and off by default**. With it off, the runtime is a fully
offline, single-machine library and none of the distributed code is compiled.
For the architectural design, see [`ARCHITECTURE.md`](ARCHITECTURE.md) §11.

---

## 1. The one idea you must internalize

**Compiled code cannot travel the wire.** A SushiRuntime task is a C++ closure
over local pointers — neither the machine code nor the pointers mean anything on
another machine. So the distributed layer never serializes code or pointers. It
serializes only *references*:

- a **kernel reference** — an `OpID` (a compile-time FNV-1a hash of a name like
  `"vec.add"_op`) for a kernel that is **already compiled into every worker
  binary**; and
- **data references** — `BufferHandle`s (`GlobalBufferID` + `version` + size) that
  each worker resolves to its own local USM.

Everything else in the design follows from this: every node runs the same binary
(so every `OpID` resolves locally), and data coherence is a versioning protocol
rather than pointer shipping.

---

## 2. When to use it (and when not to)

Use the distributed layer when you have **large, independent, offloadable tasks**
and more than one machine to run them on — the classic master + N workers fan-out.

Do **not** reach for it to speed up small or tightly-dependent graphs. The
default policy deliberately keeps payloads below **64 KiB** local, because the
network round trip would cost more than the work saved. The home machine running
8 of 10 small jobs itself is the intended steady state; offload is the exception
the policy opts into, not the rule.

---

## 3. Building with the distributed layer

It is guarded by the CMake option `SR_ENABLE_DISTRIBUTED` (default **OFF**),
which defines the `SR_DISTRIBUTED` macro. Turn it on via the CLI:

```bash
sr project build --distributed
```

or with CMake directly:

```bash
cmake -B build -G Ninja -DSR_ENABLE_DISTRIBUTED=ON ...
cmake --build build
```

When the flag is off, none of `include/SushiRuntime/distributed/` or
`src/distributed/` is compiled — zero code, zero extra third-party dependencies
(the TCP transport is raw sockets: POSIX + Winsock), and zero ABI surface.

The distributed unit/integration tests join the functional suite only when the
layer is enabled:

```bash
sr project build --distributed
sr project test --suite functional --distributed
sr project run sr_functional_tests -- --gtest_filter='*Distributed*'
```

---

## 4. Running the demo

The same binary plays either role, selected by the `--distributed=master|worker`
flag the `sr` CLI forwards to your application's `main()`. The bundled
`examples/distributed_demo.cpp` (built as `sr_distributed_demo` in distributed
builds) offloads a vector-add.

**Two processes on one host** (the worker retries until the master is up):

```bash
sr project run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555 &
sr project run sr_distributed_demo --distributed=master --port=5555 --workers=1
```

**Across machines:** run the master on one box, then start a worker on each other
box pointing `--master` at the master's IP:

```bash
# on the master host
sr project run sr_distributed_demo --distributed=master --port=5555 --workers=4

# on each worker host
sr project run sr_distributed_demo --distributed=worker --master=10.0.0.1:5555
```

Demo flags: `--port` (master listen port; `0` lets the OS pick), `--workers`
(how many to wait for), `--master=host:port` (worker's target), `--n` (vector
length).

---

## 5. The two roles in code

### Master

A normal `RuntimeContext` is *promoted* to master. You give it a connected
transport, a kernel registry, and (optionally) an offload policy:

```cpp
auto transport = TcpTransport::make_master(/*port=*/5555, /*workers=*/4);
auto policy    = std::make_shared<LeastLoadedOffloadPolicy>(/*min_offload_bytes=*/0);
ctx.enable_distributed_master(transport, make_sushi<KernelRegistry>(), policy);

// Register the USM buffers that may cross the wire.
ctx.register_distributed_buffer(a,   n * sizeof(float));
ctx.register_distributed_buffer(b,   n * sizeof(float));
ctx.register_distributed_buffer(out, n * sizeof(float));
```

> **Fluent shortcut.** With the fluent API, `Runtime::cluster(transport, kernels,
> policy)` promotes the runtime to Master and returns an `API::Cluster` handle that
> bundles the tuning knobs and telemetry (`wait_for_workers`, `task_deadline`,
> `liveness_timeout`, `offloaded()`/`stale_writebacks()`/…), and
> `Buffer<T>::distributed()` registers a buffer inline. `State<T>` has no
> `distributed()` — its per-step pointer swap is incompatible with the
> version-by-pointer coherence model, so stage a `Buffer` instead.

Until `enable_distributed_master` is called, the context behaves exactly as
Standalone (zero overhead). A task becomes offloadable only if its
`TaskMetadata::op_id` is set to an `OpID` that some live worker supports — you set
that when building the graph:

```cpp
TaskMetadata meta{};
meta.name  = "demo_vec_add";
meta.op_id = "vec.add"_op;        // names the remote kernel
meta.set_param<uint64_t>(0, n);   // scalar params travel with the task
graph.add_task(meta, {a, b}, {out}, host_fallback_lambda);
```

The lambda you pass is the **local fallback** — it runs only if the task is *not*
offloaded (policy declined, no live worker, or a retry after a worker died).

### Worker

A worker is a thin `WorkerAgent` wrapped around its own local `RuntimeContext`.
It registers the kernels this binary knows how to run, then serves dispatches:

```cpp
RuntimeContext ctx;                                  // worker's own NUMA-aware runtime
auto transport = TcpTransport::make_worker(host, port);
auto kernels   = make_sushi<KernelRegistry>();
kernels->register_kernel("vec.add"_op,
    [](sycl::queue& q, std::span<void* const> ins, std::span<void* const> outs,
       const TaskMetadata& meta) -> sycl::event { /* … */ });

WorkerAgent agent(ctx, transport, kernels);
agent.start();   // HELLO the master, then process dispatches until stop()
```

Inside the worker, the existing NUMA-aware scheduler runs the offloaded work
unchanged — the agent just feeds it tasks that arrived over the wire.

> **The `OpID` contract across machines.** A worker advertises its set of
> registered `OpID`s in its HELLO. The master only flags a task offloadable if
> some live worker advertises that op. The hash is computed identically at compile
> time in every binary, so `"vec.add"_op` on the master is bit-for-bit the same
> id the worker registered. **Register the same op under the same string on every
> node**; a typo silently makes the task non-offloadable (it runs locally).

---

## 6. The wire protocol

Every message is a tiny frame: `[4-byte length][1-byte type][payload]`. Message
types (`MsgType`):

| Type | Direction | Meaning |
|------|-----------|---------|
| `REGISTER_HELLO` | worker → master | "I'm alive; here is my OpID set." |
| `REGISTER_ACK` | master → worker | agreed common kernel set |
| `TASK_DISPATCH` | master → worker | run this `OpID` with these handles |
| `BUFFER_REQUEST` | worker → master | "I lack {id, version}; send bytes." |
| `BUFFER_PUSH` | either | raw bytes for one buffer version |
| `TASK_RESULT` | worker → master | task done; output handles ready |
| `HEARTBEAT` | worker → master | unsolicited liveness ping |
| `PING` / `PONG` | master ↔ worker | RTT probe + free-memory report |

Payloads are serialized with a tiny length-prefixed `ByteWriter`/`ByteReader`
(trivially-copyable values + sized blobs). There is no schema library: both ends
run the same binary and, in the target lab, the same architecture/endianness, so
the encoding is kept raw and minimal. **This implies the cluster must be
homogeneous in architecture/endianness** — a cross-endian cluster is out of scope
for v1.

A `TaskDispatchMsg` carries the `task_id`, the `OpID`, the serializable
`TaskMetadata` (including the 8 inline scalar `params`), and the input/output
`BufferHandle` lists — **no code, no pointers**.

---

## 7. Data coherence: versioned, pull-based, master-authoritative

The master is the single source of truth for every registered buffer. Workers
cache buffers keyed by `{GlobalBufferID, version}` and **pull on miss**:

1. The master dispatches a task referencing input handles, each with a `version`.
2. For any handle the worker doesn't have at that version, it sends a
   `BUFFER_REQUEST`; the master replies with `BUFFER_PUSH` (the raw bytes).
3. The worker runs the kernel and caches the outputs under their **new** versions.
4. `TASK_RESULT` reports the output handles. Output bytes are **lazy** — they
   travel back to the master (or on to another worker) only when something later
   actually needs them as an input.

**Invalidation is automatic.** When a task *writes* a registered buffer, the
master bumps that buffer's version (the same WAW/WAR signal the
`DependencyTracker` produces locally). A worker holding the old version now sees a
miss on its next use and re-pulls. The end-to-end test
`AutoVersionBumpAcrossLocalWriteThenOffloadedRead` pins exactly this: a local
write to `X` bumps its version so a dependent offloaded reader sees the fresh
bytes.

This means **you must register a buffer** (`register_distributed_buffer`) before a
task that touches it can be offloaded — an unregistered pointer has no handle to
ship. Local writes to a registered buffer are tracked for you; if you mutate a
registered buffer through a path the runtime can't see, call
`bump_distributed_buffer(ptr)` to force the invalidation.

---

## 8. The offload policy

`compile()` stamps which nodes are *offloadable* (op supported by a live worker).
At dispatch, for each such node the scheduler calls the coordinator behind a
single `[[unlikely]]` branch, which asks the `IOffloadPolicy` where to send it.

The default `LeastLoadedOffloadPolicy` reasons over live `WorkerStats`:

| Field | Source |
|-------|--------|
| `alive` | HELLO + liveness monitor |
| `inflight` | tasks currently dispatched to the worker |
| `free_mem` | the worker's last `PONG` (device-memory headroom; `0` = unknown) |
| `rtt_micros` | the master's `PING`/`PONG` round-trip probe |

It ranks live workers by an estimated completion cost — **RTT + transfer time +
queue backlog** — excludes any worker whose `free_mem` is below the payload size
(a `0` is treated as "unknown", so the worker stays eligible), and returns
`std::nullopt` ("run locally") when no worker qualifies or the payload is below
`min_offload_bytes` (default **64 KiB**). A worker node is simply the most distant
tier of the same locality hierarchy the NUMA scheduler already uses.

The policy is swappable: implement `IOffloadPolicy::pick_worker` and pass your
instance to `enable_distributed_master`. **Returning `nullopt` is always safe** —
the task falls back to local execution.

---

## 9. Failure handling: a dead worker never hangs the master

Workers heartbeat (default every **1 s**, `WorkerAgent::set_heartbeat_interval`).
The master's liveness monitor declares a worker dead if it is silent longer than
the timeout (default **5 s**, `RuntimeContext::distributed_set_liveness_timeout`);
any inbound message refreshes liveness, so this only fires on a genuinely
crashed/hung peer.

When a worker dies with a task in flight, the coordinator **re-dispatches** the
orphaned node — to a surviving worker, or, if none remain (or a per-node retry cap
is exceeded), it pins the node local and runs the fallback. The graph still
completes with the correct result. The `WorkerDeathFallsBackToLocal` test proves
the master's `execute()` *returns* (does not deadlock) when the worker a task was
offloaded to never answers.

---

## 10. Telemetry

The master exposes counters for observability:

```cpp
ctx.distributed_offloaded_count();     // tasks actually shipped to a worker
ctx.distributed_declined_count();      // offloadable tasks the policy kept local
ctx.distributed_redispatched_count();  // tasks re-injected after a death/failure
ctx.distributed_alive_workers();       // live registered workers
ctx.distributed_supports(op);          // can the cluster run this OpID?
```

---

## 11. Testing without a cluster: `InProcessTransport`

You don't need real machines (or even a network) to develop and test the full
protocol. `ITransport` has two implementations:

- **`TcpTransport`** — real raw-socket TCP between machines.
- **`InProcessTransport`** — runs several "nodes" in one process over loopback
  queues, exercising the *entire* master/worker protocol on a single machine
  (including under WSL). It is the network analogue of the topology layer's
  `RemappingTopologyProvider`.

Because the coordinator and worker agent depend only on `ITransport`, the jump
from single-machine simulation to a real cluster changes only the endpoint
list — never the upper layers. The distributed E2E suite runs both the in-process
fabric and a real TCP loopback path, byte-for-byte identical above the transport.

---

## 11a. Reliability & failure model

The distributed layer is an **offload accelerator**, not a highly-available
database. Its safety contract is deliberately narrow and worth stating exactly,
because what it does *not* promise is as important as what it does.

**The safety property.** The master is the single authoritative writer of every
registered buffer. The contract is **determinism under faults**: whatever the
network does — drop, duplicate, reorder, delay, or partition messages, or kill a
worker — a run that completes ends with the **same bytes** in the master's
buffers as a fault-free run. The `Integration_DistributedJepsenTest` soak asserts
exactly this under a seeded random nemesis.

**What protects that property.** Five mechanisms compose:

1. **Worker heartbeats** (`set_liveness_timeout`, default 5 s). A silent worker is
   declared dead and its in-flight tasks re-dispatched.
2. **Per-task deadline** (`distributed_set_task_deadline`, default 30 s). A second,
   independent axis: a task with no result within the bound is re-dispatched even
   if its worker is *still heartbeating*. This closes the gap where a single lost
   `TASK_DISPATCH`/`TASK_RESULT` would otherwise hang a task forever.
3. **Version-fenced writeback.** Each (re)dispatch stamps a fresh version onto the
   task's outputs; the master rejects any writeback carrying an older version, so a
   slow/zombie worker's late result can never clobber a newer one (a Kleppmann
   fencing token).
4. **Worker idempotency.** A duplicated dispatch (transport retransmit) is
   deduplicated by `task_id`, so the kernel runs at most once per id.
5. **Transparent local fallback.** After `MAX_OFFLOAD_ATTEMPTS` failed remote
   tries, the node is pinned local and runs on the master — so a permanently
   unreachable worker degrades to a slower local run, never a stall.
6. **Result atomicity.** A task's output bytes ride *inline* in its `TASK_RESULT`,
   so completion and data arrive together — the master can never mark a task done
   while its output silently failed to arrive.
7. **Registration retry.** A worker re-sends its `HELLO` every heartbeat until the
   master acknowledges it, so a lost registration message cannot strand a worker
   outside the cluster.

Workers also bound their input-pull wait (`set_pull_timeout`) and their job queue
(`set_max_jobs`, fast-failing when full), so a lost buffer push or a flood cannot
hang or balloon a worker. The whole set is exercised by the seeded
fault-injection soak (`Integration_DistributedJepsenTest`), which drops,
duplicates, reorders, holds, and partitions messages — registration included —
and asserts byte-identical results every run.

**What is explicitly NOT provided (by design).**

- **No master failover.** The master is a **single point of failure**: if it dies,
  the run dies. This is intentional for an offload accelerator; if you need an HA
  control plane, that belongs above this layer, not inside it.
- **No cross-run durability.** Buffer versions and the cluster are in-memory and
  per-run; there is no persistence or recovery log.
- **No Byzantine tolerance.** Workers are trusted to run the registered kernel
  faithfully; the layer defends against *crashes and lost messages*, not malice.
  (See [`SECURITY.md`](../../SECURITY.md) for the trust boundary.)

---

## 12. v1 limitations (know these before you scale out)

- **Homogeneous cluster only.** Raw trivially-copyable serialization assumes the
  same architecture/endianness on every node.
- **Same binary on every node.** Offload resolves an `OpID` to a kernel compiled
  into the worker; a worker can only run ops it registered.
- **No transport encryption/auth.** The TCP transport is a bare socket layer
  intended for a trusted LAN, not the public internet. See
  [`SECURITY.md`](../../SECURITY.md).
- **Coherence is intentionally simple:** master-authoritative, pull-on-miss,
  versioned. There is no peer-to-peer worker cache sharing in v1.

---

## 13. See also

- [`ARCHITECTURE.md`](ARCHITECTURE.md) §11 — how the distributed layer fits the
  rest of the runtime, and why it is zero-cost when off.
- [`INTRODUCTION.md`](INTRODUCTION.md) — the standalone programming model the
  workers run internally.
- [`CLI_GUIDE.md`](CLI_GUIDE.md) — `sr project build --distributed`,
  `sr project run ... --distributed=ROLE`.
- `examples/distributed_demo.cpp` — the runnable master/worker reference.
