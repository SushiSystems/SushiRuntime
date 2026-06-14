# Distributed SushiRuntime — Feature Tracker

Lightweight, opt-in **master-worker task offloading**, no MPI. The runtime stays
a fully offline single-machine library by default; cluster mode is engaged
explicitly, one command, only for the heavy jobs that benefit.

**Status:** core complete and verified (8 distributed tests green + a live
two-process TCP demo). What remains is production hardening — see *Remaining*.

---

## Core principles (decided, locked)

1. **Offline-first.** Standalone is the default and pays zero cost. Distributed
   engages only for tasks `compile()` flags offloadable; the scheduler hot path
   is one `[[unlikely]]` bool branch (`Node::offloadable_`).
2. **No code on the wire.** Ship an `OpID` (kernel already compiled into every
   node) + `GlobalBufferID`+version (data each worker resolves to local USM).
   Never closures or pointers.
3. **Stateful, pull-based, master-authoritative buffers.** Workers cache by
   `{id, version}`, pull on miss. A write auto-bumps the version → stale caches
   re-pull.
4. **OpID + transparent local fallback.** Unregistered kernel/buffers, or a
   policy decline, run the task locally.
5. **Substitutable transport.** `ITransport` hides the wire: `InProcessTransport`
   (single-machine cluster sim) and `TcpTransport` (raw sockets).
6. **Role via config flag.** `RuntimeRole::{Standalone, Master, Worker}`,
   default `Standalone`. Same binary, three modes.

---

## Build & run

```bash
sr project build                         # standalone (distributed not compiled)
sr project build --distributed [--clean] # distributed layer ON

# Two processes on one host (same container/machine):
sr project run sr_distributed_demo --distributed=worker --master=127.0.0.1:5555 &
sr project run sr_distributed_demo --distributed=master --port=5555 --workers=1

# Across machines: one master; on each worker box point --master at its IP.

sr project run sr_functional_tests --gtest_filter='Distributed*'
```

CMake: `-DSUSHI_ENABLE_DISTRIBUTED=ON`. `SUSHI_DISTRIBUTED` is a **PUBLIC**
compile def (it changes `Graph::Node` layout); build scripts reconfigure when the
toggle flips (stamp: `build/.sushi_dist`).

---

## ✅ Done (one-line summary; see git history / code for detail)

- **Protocol** (`protocol.hpp`): ids, `BufferHandle`, framing, `ByteWriter/Reader`,
  message (de)serialization.
- **Transports**: `InProcessTransport` (sim) + `TcpTransport` (POSIX/Winsock raw
  sockets, worker connect-retry, `shutdown()`-before-close clean stop).
- **Registries/policy**: `KernelRegistry`, opt-in `BufferRegistry` (versioned),
  `LeastLoadedOffloadPolicy`.
- **Coordinator** (master): HELLO/common-set, `try_offload`, buffer serving,
  result→successor release, "ran where" telemetry (offloaded/declined/alive).
- **Worker agent**: HELLO, dedicated exec thread (no pull/pump deadlock),
  `{id,version}` cache w/ pull-on-miss, eager result writeback.
- **Integration** (guarded): Node offload fields; `compile()` stamping; scheduler
  dispatch branch + `complete_offloaded_node`; auto version-bump via EventPolling
  observer; `RuntimeContext::enable_distributed_master()` + ordered teardown.
- **Tooling/demo**: CLI `--distributed` build+run; `examples/distributed_demo.cpp`.
- **Tests** (8, green): protocol/transport/registry units; SYCL e2e — single
  offload, auto version-bump (local write → offloaded read), 2-worker fan-out,
  offload over TCP loopback.

---

## 🔜 Remaining — production hardening (next session)

In rough priority order:

1. **Worker-death resilience.** Heartbeat to detect a dead/slow worker; treat its
   loss like a steal-tier event — re-dispatch its in-flight tasks to another
   worker or fall back to local. Today a dropped worker strands its tasks.
2. **Reconnect.** Worker re-establishes the connection if it drops mid-run
   (currently connect-retry only happens at startup).
3. **Shutdown robustness.** Unblock a master `accept()` still waiting on absent
   workers at stop (self-pipe / non-blocking accept). Today shutdown is clean
   only once all expected workers have connected.
4. **Pipelining.** Overlap comms with compute — transmit task N+1 while the
   worker runs N — to hide network latency.
5. **Worker uses its full scheduler.** Run offloaded work through the worker's
   own TaskGraph/NUMA-aware scheduler instead of directly on `get_queue(0)`, so
   intra-node stealing applies to remote work too.
6. **Lazy result fetch (optional).** Replace eager output writeback with
   pull-when-needed for non-leaf outputs, to cut backhaul traffic.

### Known smaller gaps
- `TcpTransport`: native-endian length prefix (homogeneous-lab assumption);
  light data race on the `peers` vector between accept and send (benign in
  practice, worth a lock/atomic).
- Endpoint config is direct CLI flags (`--master/--port/--workers`); no
  `nodes.txt` file parsing yet.

---

## Module map

```
include|src/SushiRuntime/distributed/
  protocol.hpp                    wire format, ids, (de)serialization
  i_transport.hpp                 transport abstraction
  in_process_transport.{hpp,cpp}  single-machine cluster simulator
  tcp_transport.{hpp,cpp}         raw-socket TCP transport (POSIX + Winsock)
  kernel_registry.{hpp,cpp}       OpID -> compiled kernel
  buffer_registry.{hpp,cpp}       void* <-> GlobalBufferID + version (master)
  i_offload_policy.hpp + offload_policy.cpp   where-to-run decision
  distributed_coordinator.{hpp,cpp}  master brain
  worker_agent.{hpp,cpp}          worker daemon
examples/distributed_demo.cpp     runnable master/worker entry (sr_distributed_demo)
tests/functional/unit/test_distributed.cpp           hardware-free units
tests/functional/integration/test_distributed_e2e.cpp  SYCL end-to-end
```
