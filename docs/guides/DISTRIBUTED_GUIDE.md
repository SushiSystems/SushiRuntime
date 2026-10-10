# Distributed execution guide

How to run one task of a graph on another machine: start a master, start a worker, write the
kernel the worker runs, share the buffers it touches, and read what happened afterwards. What
the code does underneath is in
[`modules/extension/distributed/README.md`](../../modules/extension/distributed/README.md).

## 1. What the layer is for

A master is a normal runtime with a cluster attached. A worker is a second process, usually on a
second machine, that has registered kernels by name. When a task of the master's graph names one
of those kernels and every buffer it touches is shared, the master can send the task to the
worker: the worker pulls the input bytes, runs its own copy of the kernel and streams the output
bytes back into the master's buffers. Your program reads the result where it always did.

Compiled code does not cross the network. A task carries an `OpID`, which is a hash of the
kernel's name, and buffer handles (an id, a version and a size). The worker must therefore have
been built with a kernel registered under the same name.

Every task keeps a local fallback. A task the master does not send, or takes back, runs on the
master, so a cluster that is down costs time and not correctness.

The layer is off by default. Build it with `sr build --distributed`, which sets the CMake option
`SR_ENABLE_DISTRIBUTED`; without it nothing under `modules/extension/distributed/` is compiled.
The layer adds no third-party dependency: the TCP transport uses the platform's sockets, and
SHA-256 and HMAC are written in the module.

```
sr build --distributed
sr test --distributed
sr run sr_distributed_tests -- --gtest_filter='Integration_DistributedE2ETest.*'
```

## 2. Trust model

The layer is built for a LAN whose machines you trust. It authenticates and does not encrypt.

With a shared secret, the master and a worker prove to each other at the handshake that they
hold the same key. Each side sends a fresh 32-byte nonce, and each proof is an HMAC-SHA256 over
both nonces under the secret. The worker checks the master's proof before it sends its own. The
secret never travels.

After the handshake, frames carry no signature and nothing is encrypted. So, said plainly:

| Someone who | Can | Cannot |
| --- | --- | --- |
| reaches the port without the secret | open connections, each of which the master closes after `handshake_timeout`; hold all `max_peers` slots and keep workers out (§12) | register as a worker, receive a task, read or write a buffer |
| listens on the wire | read every buffer byte, every task and every kernel id that crosses | learn the secret from a handshake, short of guessing it offline, which is why the key is 32 random bytes |
| sits on the path and can change packets | alter or inject frames after the handshake: corrupt an input on its way to a worker or an output on its way back | pass the handshake as either side; replay a recorded handshake, since both nonces are fresh |
| holds the secret | register as a worker, request the bytes of any shared buffer, return any bytes as a task's output | |

A worker is trusted to run the kernel it registered. The layer defends against a worker that
crashes, stalls or answers late. It does not defend against one that lies.

Without a secret the same handshake runs under an empty key, which authenticates nobody: any
process that reaches the port can register. `Cluster::attach` allows that only on a transport no
other machine can reach, which is the default bind of `127.0.0.1`.

## 3. Run the demo

`applications/examples/distributed_demo.cpp` is a master and
`applications/examples/distributed_worker.cpp` is its worker. Both are built by
`sr build --distributed`.

On one machine no secret is needed, because the master listens on loopback. Start the two in
either order, each in its own terminal:

```
sr run sr_distributed_demo
sr run sr_distributed_worker -- --master 127.0.0.1:5555
```

`sr run` passes what follows `--` to the program. The master prints where the task ran:

```
distributed_demo: N = 1048576, result CORRECT
  ran on: a worker
  offloaded=1 declined=0 redispatched=0 stale_writebacks=0
```

For two machines, make a key file first (§4), then:

```
sr run sr_distributed_demo -- --bind 0.0.0.0 --port 5555 --secret-file cluster.key
sr run sr_distributed_worker -- --master 192.168.1.10:5555 --secret-file cluster.key
```

The demo's own flags are listed in `applications/examples/README.md`.

## 4. Start a master

```cpp
#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>

using namespace SushiRuntime;

auto runtime = Runtime::create();

Distributed::TcpMasterOptions listener;
listener.bind = Distributed::Endpoint{"0.0.0.0", 5555};
auto transport = Distributed::TcpTransport::make_master(listener);

Distributed::ClusterOptions options;
options.secret = Distributed::SharedSecret::from_file("cluster.key");
auto cluster = Distributed::Cluster::attach(runtime, transport, std::move(options));

if (!cluster.wait_for_workers(1, std::chrono::seconds(30)))
    std::puts("no worker yet; tasks run here");
```

`Cluster::attach` builds the master, binds the port, starts it and installs it on the runtime.
It logs one line when it listens:

```
distributed master listening on 0.0.0.0:5555, protocol v2, authentication on
```

The handle it returns is a view: copies name the same master, and destroying one changes
nothing. The runtime stops the master when the runtime is destroyed; `cluster.detach()` does it
sooner. Call `attach` and `detach` only while no run is in flight. Pass a `ClusterOptions` object
or nothing as the third argument: a bare `{}` is ambiguous while the deprecated four-argument
overloads exist.

Wait for your workers before the first run. A graph decides at compile time which of its tasks
may leave, and it marks a task only when a registered worker has the kernel at that moment.

### Where it listens

`TcpMasterOptions::bind` defaults to `127.0.0.1` and port 0, which takes any free port;
`transport->local_endpoint()` returns the port once `attach` has returned. To accept workers from
other machines, write the address out: `0.0.0.0`, `::` or the address of one interface.

| `TcpMasterOptions` field | Default | Meaning |
| --- | --- | --- |
| `bind` | `127.0.0.1`, port 0 | Address and port to listen on |
| `backlog` | 16 | Pending connections the OS may queue |
| `max_peers` | 64 | Open connections, registered or not (§12) |
| `control_queue_frames` | 4096 | Control frames queued per peer before a send is refused |
| `write_idle_timeout` | 5000 ms | Closes a peer whose socket takes no byte for this long |

### When a secret is required

`attach` refuses an empty secret on a transport other machines can reach. For TCP that is every
bind address other than `127.0.0.0/8`, `::1` and `localhost`. The exception says so:

```
Cluster::attach: the transport can be reached from other machines and ClusterOptions::secret is empty; load one with SharedSecret::from_file(), or bind the master to a loopback address.
```

### Make a secret file

The secret is a file of at least 32 bytes and at most 1 MiB, read byte for byte, so both
machines must hold the same file. Make 32 random bytes once and copy the file to every machine
over a channel you trust:

```
head -c 32 /dev/urandom > cluster.key
chmod 600 cluster.key
```

On Windows, in PowerShell:

```
$key = New-Object byte[] 32
[Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($key)
[IO.File]::WriteAllBytes("$PWD\cluster.key", $key)
```

`SharedSecret::from_file(path)` throws when the file cannot be read or its length is outside
those bounds, and the message names the file. `SharedSecret::from_environment(name)` reads the
file a variable names and returns the empty secret when the variable is unset. No call takes the
secret itself as text, because a process list would show it.

### Cluster options

| `ClusterOptions` field | Default | Meaning |
| --- | --- | --- |
| `secret` | empty | The key workers must prove they hold |
| `policy` | null, which selects `LeastLoadedOffloadPolicy` | Chooses the worker of each task (§8) |
| `kernels` | null | A registry the master keeps alive; the master reads no kernel from it |
| `liveness_timeout` | 5000 ms | Silence after which a worker is closed; workers are told in `WELCOME` |
| `task_deadline` | 30000 ms | Bound on a kernel's run on a worker; 0 sets no bound |
| `handshake_timeout` | 5000 ms | Time a new connection has to finish the handshake |
| `chunk_bytes` | 262144 | Data bytes per buffer chunk, from 4096 to 1048512 |

`cluster.liveness_timeout(...)` and `cluster.task_deadline(...)` change the two timings on a
running master. A worker that is already registered keeps the liveness value its `WELCOME`
carried.

`attach` throws `Core::SushiException` when the transport is null, the secret is missing on a
reachable transport, `chunk_bytes` is out of range or the master does not start. It starts the
new master before it touches one that is already attached, so a failed `attach` leaves that one
installed and working.

## 5. Start a worker

A worker is your own program, because the kernels are compiled into it. Its `main` registers
them and hands over to `run_worker`:

```cpp
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>

using namespace SushiRuntime;

int main(int argc, char** argv)
{
    auto kernels = make_sushi<Distributed::KernelRegistry>();
    kernels->register_kernel(K_VECTOR_ADD, add_vectors);
    return Distributed::run_worker(argc, argv, kernels);
}
```

`K_VECTOR_ADD` and `add_vectors` are the id and the kernel of §6.

`run_worker` reads the flags, loads the secret, builds a runtime, connects, serves the master
until a signal and returns the exit code. It logs:

```
worker joining master 192.168.1.10:5555, protocol v2, authentication on
connected to master 192.168.1.10:5555 (peer 1)
registered with master (peer 1) as worker 1: liveness 5000 ms, chunks of 262144 bytes, new session
```

### Flags

| Flag | Environment | Default |
| --- | --- | --- |
| `--master host:port` | `SR_MASTER` | required; a host name, an IPv4 address or `[ipv6]:port`; port 0 is refused |
| `--secret-file path` | `SR_CLUSTER_SECRET_FILE` | none |
| `--name text` | `SR_WORKER_NAME` | the machine's host name, else `worker`; at most 255 bytes |
| `--max-jobs n` | `SR_WORKER_MAX_JOBS` | 1024; tasks that may wait to run, from 1 to 4294967295 |
| `--cache-bytes n` | `SR_WORKER_CACHE_BYTES` | 0, which takes half of the device's memory |
| `--heartbeat-ms n` | `SR_WORKER_HEARTBEAT_MS` | 1000; at least 1 |
| `--pull-idle-timeout-ms n` | `SR_WORKER_PULL_IDLE_TIMEOUT_MS` | 5000; silence that fails a buffer pull, 0 waits without a limit |
| `--connect-timeout-s n` | `SR_WORKER_CONNECT_TIMEOUT_S` | 60; time allowed to reach the master the first time, 0 waits without a limit |
| `--no-reconnect` | | reconnecting is on |
| `--log-level name` | | unchanged; `off`, `error`, `warn`, `info` or `debug` |
| `--version` | | prints `sushiruntime worker, protocol v2, build <version>` and exits 0 |
| `--help` | | prints the flags and exits 0 |

A flag beats its environment variable, which beats the default. A variable set to nothing counts
as unset. A value follows its flag as the next argument or after `=`.

`run_worker(argc, argv, kernels)` refuses a flag it does not know. If your worker has flags of
its own, parse the command line yourself, fill a `WorkerOptions` and call
`run_worker(options, kernels)`. That overload reads no environment variable.

### Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Stopped by a signal, in order; also `--help` and `--version` |
| 1 | A bad or unknown flag, no master given, an unknown log level, or a secret file that cannot be read or holds fewer than 32 bytes. One line on the error stream, prefixed `sushiruntime worker: `, says which |
| 2 | The master was not reached within the connect timeout |
| 3 | Authentication failed: the secrets of the two machines differ |
| 4 | The master speaks another protocol version, or the peer is not a SushiRuntime master |
| 5 | The master was lost and `--no-reconnect` was given |
| 70 | An exception reached `run_worker`, the worker did not start, or the registry is null |
| 130 | A second signal ended the process at once |

### Signals

The first SIGINT or SIGTERM (and SIGBREAK on Windows) asks the worker to stop. It takes no more
tasks, reports the queued ones as shutting down, lets the running kernel finish, sends `GOODBYE`
and exits 0. The master takes the worker's tasks back at once instead of waiting out its
liveness timeout, and logs:

```
worker 1 "node-b" 192.168.1.11:50312 lost: said GOODBYE (shutdown): worker stopping; 0 tasks returned to the scheduler
```

The worker notices the signal within 100 ms. A second signal ends the process immediately with
code 130 and runs no destructor.

### Reconnection

The worker tries the master again after every failure, waiting 100 ms and doubling up to 5 s. It
resolves the host name on every attempt. `--connect-timeout-s` bounds only the first connection;
once a worker has registered, it reconnects for as long as it runs, unless `--no-reconnect` was
given.

A worker that reconnects is a new worker to the master, with a new id. When it finds a master
process other than the one it knew, its `registered` line ends in `new session` and it empties
its buffer cache, because buffer ids start again in every master process.

### A worker inside your own process

`WorkerAgent` (`worker_agent.hpp`) is the same worker without `main`: you give it a runtime, a
worker-side transport, a registry and `WorkerAgentOptions`, then call `start()`,
`wait_for_end()` and `stop()`. The tests and the benchmark use it to run a master and a worker
in one process.

## 6. Write a kernel

A kernel is a function the worker calls with the task's buffers. It submits work on the queue it
is given and returns the event:

```cpp
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/task/task_types.hpp>

using SushiRuntime::Graph::Literals::operator""_op;

inline constexpr SushiRuntime::Graph::OpID K_VECTOR_ADD = "demo.vector_add"_op;

sycl::event add_vectors(sycl::queue& queue,
                        SushiRuntime::span<const SushiRuntime::Distributed::BufferView> inputs,
                        SushiRuntime::span<const SushiRuntime::Distributed::BufferView> outputs,
                        const SushiRuntime::Graph::TaskMetadata&)
{
    const float* a = inputs[0].as<const float>();
    const float* b = inputs[1].as<const float>();
    float* sum = outputs[0].as<float>();
    const std::size_t count = outputs[0].size_bytes / sizeof(float);
    return queue.parallel_for(sycl::range<1>(count), [a, b, sum](sycl::id<1> id)
    {
        sum[id[0]] = a[id[0]] + b[id[0]];
    });
}
```

A `BufferView` is an address and a size in bytes. The views arrive in the order the task lists
its buffers and stay valid until the returned event completes. `as<T>()` casts the address and
checks nothing, so take element counts from `size_bytes`. The task's scalar parameters, at most
eight 64-bit values, arrive in the metadata: `metadata.get_param<std::uint64_t>(0)`.

Register a kernel with `KernelRegistry::register_kernel(id, kernel)` before you hand the
registry to `run_worker`; from then on the registry is only read. The id is a hash of the name,
computed at compile time, so the master and the worker agree when they spell the name the same.
A name no worker registered leaves the task on the master, and the master says so (§7).
Workers of one cluster may register different kernels: the master offers a task only to a
worker that registered its operation.
Put the ids and the kernels in one header that both programs include, as
`applications/examples/distributed_kernels.hpp` does.

A kernel may throw. The worker then reports the task as failed and keeps running, and the master
resubmits the task. A worker runs one kernel at a time, on the queue of its device 0.

### Adding the task on the master

The fluent form names the kernel and gives the local fallback as a lambda:

```cpp
auto a = runtime.buffer<float>(count).distributed();
auto b = runtime.buffer<float>(count).distributed();
auto sum = runtime.buffer<float>(count).distributed();

const float* a_data = a.data();
const float* b_data = b.data();
float* sum_data = sum.data();

auto graph = runtime.graph();
graph.add(K_VECTOR_ADD, count, API::Reads(a, b), API::Writes(sum),
          [a_data, b_data, sum_data](std::size_t index)
          {
              sum_data[index] = a_data[index] + b_data[index];
          });
graph.run();
```

The lambda runs when the task stays on the master, so it must compute what the kernel computes.
The demo keeps the two equal by calling one function from both.

`add_registered_task()` (`registered_task.hpp`) removes the second statement of the work: it
takes a `KernelRegistry` and uses the registered kernel as the local fallback. It adds to a
`Graph::TaskGraph`, the engine's graph, which the fluent `API::Graph` does not expose, so a
fluent program uses the form above.

## 7. Share buffers

A buffer can cross only after you share it. `Buffer::distributed()` shares one buffer and
`State::distributed()` shares both allocations of a stepped field. Call them after
`Cluster::attach`:

```cpp
auto cluster = Distributed::Cluster::attach(runtime, transport, std::move(options));
auto positions = runtime.buffer<float>(count).distributed();
```

Called before `attach`, `distributed()` shares nothing and the buffer's tasks run on the master.
The first such call in a process logs:

```
distributed() was called on a runtime with no offload coordinator, so the buffer stays local and its tasks run on this machine; call Cluster::attach() before distributed(). Later calls are not logged
```

A task may leave only when all of these hold at compile time: it names an operation, a
registered worker has a kernel for that operation, every buffer it reads or writes is shared,
and it does not list one buffer twice among its writes. When a graph has tasks that name an
operation and fail one of the other three, the master logs one line per compile:

```
2 of 3 op-tagged nodes will run locally: 1 no live worker for the op, 1 buffer not shared, 0 buffer written twice (first: op <id>)
```

The master owns every shared buffer and gives each a version. A worker caches the bytes it
pulled and asks again only when the version has moved. The master raises the version whenever a
task writes the buffer, locally or on a worker. If you write a shared buffer through a path the
runtime does not see, call `cluster.bump_buffer(pointer)` so workers pull the new bytes. When a
shared buffer is released, the master tells the workers to drop it.

A worker's cache holds at most `--cache-bytes` and evicts the least recently used buffer that no
running task holds.

## 8. What the default policy does

At dispatch the master asks an `IOffloadPolicy` which worker gets the task, or none. The default
is `LeastLoadedOffloadPolicy`:

- A task whose inputs total under 64 KiB stays on the master. Only the bytes the task reads
  count; outputs do not.
- A worker whose last reported free device memory is below the input size is skipped. A worker
  that reports 0 is treated as unknown and stays eligible.
- Of the workers left, the one with the lowest estimate wins. The estimate adds the worker's
  last ping round trip, a transfer time for the inputs, and one millisecond per task already out
  on that worker.

The master offers the policy only workers with room in their job queue (`--max-jobs`), so a full
worker is never picked.

To send every task whatever its size, pass a policy with a threshold of 0:

```cpp
options.policy = std::make_shared<Distributed::LeastLoadedOffloadPolicy>(0);
```

To write your own, implement `IOffloadPolicy::pick_worker` and return the `worker_id` of one of
the workers you were offered, or nothing to run the task locally. The call runs on a pool thread
under a lock of the master, so return quickly and call nothing of the `Cluster` from it.

A task is offered to workers at most twice in one run. After the second attempt comes back, it
runs on the master.

## 9. Read the statistics

`cluster.statistics()` returns a `ClusterStatistics`. Counters count since `attach`; the fields
are read one by one, not at one instant.

| Field | Counts |
| --- | --- |
| `offloaded` | Dispatches queued to a worker. A task sent twice counts twice |
| `declined` | Dispatches of an offloadable task that ran on the master instead: the policy picked no worker, no worker had room, the task had used its two attempts, a buffer was no longer shared, or the send was refused |
| `completed_remote` | Tasks a worker finished with every output byte back on the master |
| `redispatched` | Tasks taken back from a worker and resubmitted: the worker was lost, it reported a failure, the task was reaped, or the master stopped |
| `stale_writebacks` | Output chunks the master refused, because their task was no longer out or their version was old |
| `handshake_rejected_auth` | Peers that sent a proof made with another secret |
| `handshake_rejected_version` | Peers closed for speaking another protocol version |
| `peers_lost` | Registered workers that left, for any reason |
| `peers_closed_malformed` | Peers closed for bytes that are not a frame, a bad header or a payload that does not decode |
| `tasks_reaped_deadline` | Tasks with no result within `task_deadline` |
| `tasks_reaped_unacknowledged` | Dispatches no worker confirmed within the liveness timeout |
| `tasks_reaped_lost_result` | Tasks a worker confirmed and then stopped listing in two heartbeats in a row |
| `bytes_sent`, `bytes_received` | Bytes the transport wrote and read, headers included |
| `chunks_sent` | Buffer chunks served to workers that pulled an input |
| `chunks_received` | Output chunks written into the master's buffers |
| `transfers_aborted` | Pulls the master ended early, because the buffer moved to a newer version or was released |
| `send_queue_full` | Dispatches refused because the worker's control queue was full |
| `alive_workers` | Workers registered and answering now |
| `pending_tasks` | Tasks out on a worker now |
| `workers` | One `WorkerSnapshot` per registered worker |

A `WorkerSnapshot` holds the worker's `id`, `name` and `endpoint`, its `inflight` count, the
`rtt_micros` of its last ping, the `free_mem` of its last pong, the `queue_depth` and
`cache_bytes` of its last heartbeat, and `ms_since_last_frame`.

In a healthy run `completed_remote` follows `offloaded` and the reap counters stay at 0.
`declined` above 0 with a worker connected usually means tasks under the 64 KiB threshold.
`tasks_reaped_unacknowledged` or `tasks_reaped_lost_result` above 0 means frames went missing
between the two machines.

`cluster.offloaded()`, `declined()`, `redispatched()`, `stale_writebacks()` and
`alive_workers()` return single values without building the worker list.

A worker logs its own counters once, when it stops:

```
worker stopped: 12 tasks run; failed: 0 kernel, 0 output allocation, 0 input unavailable, 0 unknown operation, 0 shutting down; cache 8388608 bytes, 0 evictions; 0 reconnects
```

## 10. What happens when something fails

A run that completes leaves the same bytes in the master's buffers as a run with no cluster. The
master takes a task back whenever it cannot be sure of the result, raises the version of the
task's outputs so late bytes from the old attempt are refused, and resubmits the task.

### A worker is lost

The worker's process dies or its connection breaks. The master logs:

```
worker 1 "node-b" 192.168.1.11:50312 lost: closed by the peer; 1 tasks returned to the scheduler
```

The cause is one of `closed by the peer`, `closed by this master`, `read or write failed`,
`write stalled`, `malformed frame` or `a frame handler failed`, followed by the OS reason in
brackets when there is one. The returned tasks go to another worker or run on the master.
`peers_lost` rises by one and `redispatched` by the number of tasks.

### A worker goes silent

A worker that sends no frame for `liveness_timeout` is closed. Every frame counts, including the
chunks of a long transfer, so a slow link is not a silent one. The master logs two lines:

```
worker 1 sent no frame for more than 5000 ms; its connection is closed and its tasks return to the scheduler
worker 1 "node-b" 192.168.1.11:50312 lost: closed by this master; 1 tasks returned to the scheduler
```

The worker, if it is still alive, sees the close, reconnects and registers again.

### A link stalls

A link stalls when the other machine stops taking bytes: the connection stays open and nothing
moves. Two timers watch for it.

- The liveness timeout closes a worker that has also stopped sending, with the two lines above.
  A write that has been stuck on that socket for at least 1 s is ended by the close, and the
  loss is reported within about 100 ms.
- `write_idle_timeout` (5 s) closes a peer whose socket takes no byte for that long, even while
  frames still arrive from it. The cause in the log is then
  `write stalled (no byte left for 5000 ms)`.

A write that had been stuck for less than 1 s when liveness closed the worker is left to the
second timer. That is the usual case when `liveness_timeout` is set below 1 s: the worker's
tasks then come back after `write_idle_timeout`, not after the liveness timeout.

### One task goes missing

The worker is alive and one task never comes back. The master takes it back and logs at most one
such line per worker per second:

```
task 42 taken back from worker 1: no result within the task deadline; it is resubmitted to the scheduler
```

The cause is one of `no result within the task deadline`,
`the worker did not acknowledge the dispatch` or `the worker's heartbeats stopped listing it`.
The last two apply even with `task_deadline` at 0.

### The master is lost

The master is the one process a run cannot lose: if it dies, the run dies with it. A worker
survives. It logs the loss, drops the tasks it had queued and reconnects:

```
master (peer 1) lost: closed by the master ; queued jobs are dropped and the transport decides whether to reconnect
cannot reach master 192.168.1.10:5555 (192.168.1.10): <OS reason>; attempt 1, next in 100 ms
```

The `cannot reach master` line is logged for the first failed attempt and every tenth after it.
A master that goes silent without closing the connection is noticed after the liveness timeout:

```
no frame from master (peer 1) for 5012 ms (limit 5000 ms); closing the connection so the transport can reconnect
```

With `--no-reconnect` the worker exits with code 5 instead.

A master that stops in order, through `detach()` or the runtime's destruction, takes back every
task that is still out and runs it locally. It logs
`distributed master stopped; 3 tasks returned to the scheduler`.

### The secrets differ

The worker checks the master's proof first, finds it wrong and closes the connection without
sending a proof of its own. It logs this and exits with code 3:

```
master (peer 1) did not prove the cluster secret; the secret files of the two machines differ; closed, not reconnecting
```

The master never sees a wrong proof, so it counts nothing and cannot name the reason. All it
logs is:

```
192.168.1.11:50312 (peer 3) closed before it registered: closed by the peer
```

When you see that line on the master, read the worker's log. `handshake_rejected_auth` and the
master line `closed before it registered: failed authentication` appear only for a peer that
does send a wrong proof, which a worker built from this code does not.

### The versions differ

Master and worker must speak the same protocol version; there is no negotiation. The master
logs this, counts `handshake_rejected_version` and closes:

```
192.168.1.11:50312 (peer 3) speaks protocol v3, this master v2; build both from one commit; closed, 0 tasks returned to the scheduler
```

The worker logs this and exits with code 4:

```
master (peer 1) speaks protocol v2, this worker v3; build both from one commit; not reconnecting
```

Something that is not a SushiRuntime program at all, such as a port scanner, is closed with
`closed before it registered: not a SushiRuntime frame (first four 0x47455420)`.

### The kernel fails on the worker

The worker logs the failure and keeps serving:

```
task 42: the kernel of operation <id> threw: <what()>; reported as kernel failed
```

The master resubmits the task and `redispatched` rises. The same happens when an output does not
fit the worker's cache (`cannot allocate ... bytes for buffer ...`), when an input does not
arrive within the pull idle timeout (`input buffer ... did not arrive`), and when the worker has
no kernel for the operation.

## 11. Protocol versions

| Version | Release | What changed |
| --- | --- | --- |
| 1 | up to v0.3.0 | The first protocol: a `[u32 length][u8 type]` frame, nine message types, no authentication, and a buffer sent whole in one frame |
| 2 | unreleased | A 12-byte header with a magic and the version. Sixteen message types with fixed-width little-endian fields. A mutual HMAC-SHA256 handshake. Buffers cross in chunks. A worker confirms each dispatch, and each side can say why it refuses or leaves |

A version 1 peer and a version 2 peer refuse each other. Build the master and every worker from
one commit. `--version` on a worker prints the version it speaks. The frame layout and the
compatibility rule are in
[`modules/extension/distributed/README.md`](../../modules/extension/distributed/README.md),
section "Protocol versions".

## 12. Limits

| Limit | What it means for you |
| --- | --- |
| A trusted LAN only | Nothing is encrypted and frames are not signed (§2) |
| No flood protection | `max_peers` (64) counts every open connection, whether or not it has registered, and a connection above the cap is refused with `REJECT(busy)`. A host that can reach the port and keeps 64 connections open, opening new ones as the handshake timeout closes the old, keeps workers out. Keep the port behind a firewall |
| Frame size | A frame's payload is at most 1 MiB. A buffer of any size crosses, because it travels in chunks; a task's dispatch, with its list of buffer handles, must fit in one frame |
| Chunk size | `chunk_bytes` is 256 KiB by default and must lie between 4096 and 1048512 (1 MiB less 64). The master chooses it and tells each worker in `WELCOME` |
| Queues | A peer's control queue holds 4096 frames; a dispatch that finds it full runs on the master and counts in `send_queue_full`. A worker holds at most `--max-jobs` waiting tasks |
| One kernel at a time | A worker runs its tasks one after another, on its device 0 |
| Two attempts | A task is offered to workers at most twice per run |
| One master | The master does not fail over, and nothing about a cluster survives its process |
| Workers present at compile | A graph compiled before a worker registered does not use that worker for an operation no earlier worker had |
| Same layout on both ends | Frame fields are little-endian on every machine, but buffer bytes cross as they lie in memory, so master and worker must agree on the byte order and layout of your element types |
| No measured figures | `tests/benchmark/benchmark_distributed_offload.cpp` measures the round trip. It has not been run yet, so this guide quotes no throughput |

Defects that are recorded and not yet fixed are in
[`KNOWN_ISSUES.md`](../reference/KNOWN_ISSUES.md).

## 13. Troubleshooting

Find the line you see. The second column says which side shows it.

| Line | Shown by | Cause and what to do |
| --- | --- | --- |
| `Cluster::attach: the transport can be reached from other machines and ClusterOptions::secret is empty` | master, as an exception | Load a secret with `SharedSecret::from_file()`, or bind to `127.0.0.1` |
| `Cluster::attach: the master did not start: cannot listen on <address>:<port>: ...` | master, as an exception | Another process holds the port, or the address is not one of this machine's. Choose another port or stop the other process |
| `cannot reach master <host>:<port> ...; attempt N, next in M ms` | worker | The master is not running yet, the address or port is wrong, or a firewall drops the connection. A master bound to `127.0.0.1` cannot be reached from another machine: bind it to `0.0.0.0` and give it a secret |
| `<address> (peer N) closed before it registered: closed by the peer` | master | Most often a worker with another secret, or with none; read the worker's log (§10). A port probe looks the same |
| `master (peer N) did not prove the cluster secret` | worker, which exits 3 | The two key files differ. Copy the master's file to the worker again and compare checksums |
| `<address> (peer N) speaks protocol vX, this master vY` | master | The two programs were built from different commits. Rebuild both from one |
| `<address> refused: 64 peers are open, the limit` | master | Every slot is taken. Raise `TcpMasterOptions::max_peers`, or find what holds the connections. The worker logs `master (peer N) takes no more workers` and tries again |
| `peer N sent no valid handshake in 5000 ms; closed` | master | Something connected and said nothing the master understands: a probe, or a worker too slow to answer. For the second, raise `handshake_timeout` |
| `N of M op-tagged nodes will run locally: ...` | master | The counts say why. `no live worker for the op`: no registered worker has that kernel, so check the name on both sides and wait for the worker before the first run. `buffer not shared`: call `distributed()` on every buffer the task touches, after `attach`. `buffer written twice`: the task lists one buffer twice among its writes |
| `distributed() was called on a runtime with no offload coordinator` | master | Move the `distributed()` call after `Cluster::attach` |
| No line: `declined` rises and `offloaded` stays 0 with a worker connected | master's statistics | The task's inputs total under 64 KiB, which the default policy keeps local (§8) |
| `worker N sent no frame for more than 5000 ms` | master | The worker hung, or the link dropped packets without closing. If the workers are slow and healthy, raise `liveness_timeout` |
| `task T taken back from worker N: no result within the task deadline` | master | The kernel runs longer than `task_deadline`. Raise it, or set it to 0 for no bound |
| `task T: input buffer B version V did not arrive` | worker | The pull went quiet for the pull idle timeout. Check the link; on a slow one raise `--pull-idle-timeout-ms` |
| `task T: cannot allocate N bytes for buffer B (budget X, in use Y)` | worker | The buffers of one task do not fit the cache. Raise `--cache-bytes` |

## 14. See also

- [`modules/extension/distributed/README.md`](../../modules/extension/distributed/README.md):
  how the code is built, class by class.
- [`ARCHITECTURE.md`](../architecture/ARCHITECTURE.md) §12: where the layer sits in the runtime.
- [`CLI_GUIDE.md`](CLI_GUIDE.md): `sr build`, `sr test` and `sr run`.
- `applications/examples/distributed_demo.cpp`, `applications/examples/distributed_worker.cpp`
  and `applications/examples/distributed_kernels.hpp`: the programs of §3.
