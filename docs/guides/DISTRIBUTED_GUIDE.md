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

Compiled code does not cross the network. A task carries an `OperationID`, which is a hash of
the operation's name, its name and parameters, and buffer handles (an id, a version and a size). The worker must therefore have
been built with a kernel registered under the same name.

Every task keeps a local fallback: the kernel given to the verb that recorded it. A task the
master does not send, or takes back, runs on the master, so a cluster that is down costs time
and the result stays correct.

The layer is off by default. Build it with `sr build --distributed`, which sets the CMake option
`SR_ENABLE_DISTRIBUTED`; without it nothing under `modules/extension/distributed/` is compiled.
The layer adds no third-party dependency: the TCP transport uses the platform's sockets, and
SHA-256 and HMAC are written in the module. `sr test --distributed` runs its tests.

## 2. Trust model

The layer is built for a LAN whose machines you trust. It authenticates and does not encrypt.

With a shared secret, the master and a worker prove to each other at the handshake that they
hold the same key. Each side sends a fresh 32-byte nonce, and each proof is an HMAC-SHA256 over
both nonces under the secret. The worker checks the master's proof before it sends its own. The
secret never travels.

After the handshake, frames carry no signature and nothing is encrypted:

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
`applications/examples/distributed_worker.cpp` is its worker. `sr build --distributed` builds
both. On one machine no secret is needed, because the master listens on loopback. Start the two
in either order, each in its own terminal:

```
sr run sr_distributed_demo
sr run sr_distributed_worker -- --master 127.0.0.1:5555
```

[The examples README](../../applications/examples/README.md) has the demo's flags, its output
and the two commands for two machines.

## 4. Start a master

```cpp
        auto runtime = Runtime::create().value();

        Distributed::TCPMasterOptions listener;
        listener.bind = options.listen;
        auto transport = Distributed::TCPTransport::make_master(listener);

        auto secret = read_secret(options.secret_file);
        if (!secret)
        {
            std::fprintf(stderr, "distributed_demo: %s\n", secret.error().message.c_str());
            return 1;
        }

        Distributed::ClusterOptions cluster_options;
        cluster_options.secret = std::move(*secret);
        const bool is_authenticated = !cluster_options.secret.empty();
        auto attached =
            Distributed::Cluster::attach(runtime, transport, std::move(cluster_options));
        if (!attached)
        {
            std::fprintf(stderr, "distributed_demo: %s\n", attached.error().message.c_str());
            return 1;
        }
        Distributed::Cluster& cluster = *attached;
```

Source: `applications/examples/distributed_demo.cpp`

The demo includes `<SushiRuntime/distributed/distributed.hpp>` beside the umbrella header.
`options.listen` is the `Endpoint` the demo built from `--bind` and `--port`, and `read_secret`
calls `SharedSecret::from_file` when a file was named.

`Cluster::attach` builds the master, binds the port, starts it and installs it on the runtime.
It returns a `Result<Cluster>` and logs one line when it listens, which starts
`distributed master listening on` and names the address, the protocol version and whether
authentication is on.

The handle it returns is a view: copies name the same master, and destroying one changes
nothing. The runtime stops the master when the runtime is destroyed; `cluster.detach()` does it
sooner. `attach` returns `InvalidState` while a run is in flight.

Wait for your workers before the first run, with `cluster.wait_for_workers(count, timeout)`. A
graph decides at compile time which of its tasks
may leave, and it marks a task only when a registered worker has the kernel at that moment.

### Where it listens

`TCPMasterOptions::bind` defaults to `127.0.0.1` and port 0, which takes any free port;
`transport->local_endpoint()` returns the port once `attach` has returned. To accept workers from
other machines, write the address out: `0.0.0.0`, `::` or the address of one interface.

| `TCPMasterOptions` field | Default | Meaning |
| --- | --- | --- |
| `bind` | `127.0.0.1`, port 0 | Address and port to listen on |
| `backlog` | 16 | Pending connections the OS may queue |
| `max_peers` | 64 | Open connections, registered or not (§12) |
| `control_queue_frames` | 4096 | Control frames queued per peer before a send is refused |
| `write_idle_timeout` | 5000 ms | Closes a peer whose socket takes no byte for this long |

### When a secret is required

`attach` refuses an empty secret on a transport other machines can reach. For TCP that is every
bind address other than `127.0.0.0/8`, `::1` and `localhost`. The error's message says so; §13 quotes it.

### Make a secret file

The secret is a file of at least 32 bytes and at most 1 MiB, read byte for byte, so both
machines must hold the same file. Make 32 random bytes once and copy the file to every machine
over a channel you trust:

```
head -c 32 /dev/urandom > cluster.key
chmod 600 cluster.key
```

[The examples README](../../applications/examples/README.md) has the PowerShell form.

`SharedSecret::from_file(path)` returns a `Result`: the secret, or `InvalidAccess` when the file
cannot be read or its length is outside those bounds, with a message that names the file. `SharedSecret::from_environment(name)` reads the
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
running master; `liveness_timeout` returns `InvalidState` for a value below 1 ms. A worker that is already registered keeps the liveness value its `WELCOME`
carried.

`attach` returns `InvalidState` when a run is in flight, `liveness_timeout` or
`handshake_timeout` is below 1 ms, the secret is missing on a reachable transport, `chunk_bytes`
is out of range or the master does not start. A null transport stops the process. `attach`
starts the new master before it touches one that is already attached, so a failed `attach`
leaves that one installed and working.

## 5. Start a worker

A worker is your own program, because the kernels are compiled into it. Its `main` registers
them and hands over to `run_worker`:

```cpp
#include <SushiRuntime/distributed/distributed.hpp>

#include "distributed_kernels.hpp"

using namespace SushiRuntime;

int main(int argc, char** argv)
{
    auto kernels = make_sushi<Distributed::KernelRegistry>();
    DistributedDemo::register_kernels(*kernels);

    // run_worker() reads the flags, serves the master until a signal and returns the exit code.
    return Distributed::run_worker(argc, argv, kernels);
}
```

Source: `applications/examples/distributed_worker.cpp`

`register_kernels` is the function of §6.

`run_worker` reads the flags, loads the secret, builds a runtime, connects, serves the master
until a signal and returns the exit code.

### Flags, exit codes and signals

`--master host:port` is the one required flag; `--secret-file path` names the key file. `--help`
prints every flag, and each flag that takes a value has an environment variable. The tables of
flags, defaults and exit codes are in [the module README](../../modules/extension/distributed/README.md),
section "worker_main.hpp". The codes a deployment meets: 2 when the master was not reached within
the connect timeout (60 s by default), 3 when the secrets of the two machines differ, 4 when the
master speaks another protocol version, 5 when the master was lost and `--no-reconnect` was
given.

`run_worker(argc, argv, kernels)` refuses a flag it does not know. If your worker has flags of
its own, parse the command line yourself, fill a `WorkerOptions` and call
`run_worker(options, kernels)`. That overload reads no environment variable.

The first SIGINT or SIGTERM (and SIGBREAK on Windows) asks the worker to stop. It takes no more
tasks, reports the queued ones as shutting down, lets the running kernel finish, sends `GOODBYE`
and exits 0. The master takes the worker's tasks back at once. A second signal ends the process
with code 130 and runs no destructor.

### Reconnection

The worker tries the master again after every failure, waiting 100 ms and doubling up to 5 s. It
resolves the host name on every attempt. `--connect-timeout-s` bounds only the first connection;
once a worker has registered, it reconnects for as long as it runs, unless `--no-reconnect` was
given.

A worker that reconnects is a new worker to the master, with a new id. When it finds a master
process other than the one it knew, its `registered` line ends in `new session` and it empties
its buffer cache, because buffer ids start again in every master process.

### A worker inside your own process

`WorkerAgent` is the same worker without `main`, which the tests use to run a master and a
worker in one process; the module README describes it under "worker_agent.hpp".

## 6. Write a kernel

A kernel is a function the worker calls with the task's buffers. It submits work on the queue it
is given and returns the event:

```cpp
    /** @brief Names the operation that adds two float vectors element by element. */
    inline constexpr auto K_VECTOR_ADD = "demo.vector_add"_op;
```

Source: `applications/examples/distributed_kernels.hpp`

```cpp
    inline sycl::event add_vectors(
        sycl::queue& queue, SushiRuntime::span<const SushiRuntime::Distributed::BufferView> inputs,
        SushiRuntime::span<const SushiRuntime::Distributed::BufferView> outputs,
        const SushiRuntime::NodeOptions&)
    {
        const float* a = inputs[0].as<const float>();
        const float* b = inputs[1].as<const float>();
        float* sum = outputs[0].as<float>();
        const std::size_t count = outputs[0].size_bytes / sizeof(float);
        return queue.parallel_for(sycl::range<1>(count), [a, b, sum](sycl::id<1> id)
        {
            add_element(a, b, sum, id[0]);
        });
    }

    /** @brief Registers the kernel of every operation of the demo. */
    inline void register_kernels(SushiRuntime::Distributed::KernelRegistry& kernels)
    {
        kernels.register_kernel(K_VECTOR_ADD, add_vectors);
    }
```

Source: `applications/examples/distributed_kernels.hpp`

Both blocks are in the namespace `DistributedDemo`. `add_element()` of the same header writes
one element of the sum.

A `BufferView` is an address and a size in bytes. The views arrive in the order the task lists
its buffers and stay valid until the returned event completes. `as<T>()` casts the address and
checks nothing, so take element counts from `size_bytes`. The last argument is the node's
`NodeOptions`: its name, its operation and its `parameters`, at most eight 64-bit values.

Register a kernel with `KernelRegistry::register_kernel(id, kernel)` before you hand the
registry to `run_worker`, which seals it; registering on a sealed registry breaks a
precondition. The id is a hash of the name,
computed at compile time, so the master and the worker agree when they spell the name the same.
A name no worker registered leaves the task on the master, and the master says so (§7).
Workers of one cluster may register different kernels: the master offers a task only to a
worker that registered its operation.
Put the ids and the kernels in one header that both programs include, as
`applications/examples/distributed_kernels.hpp` does.

A kernel may throw. The worker catches the exception, reports the task as failed and keeps
running, and the master resubmits the task. A worker runs one kernel at a time, on the queue of its device 0.

### Adding the task on the master

The node states the operation in its `NodeOptions`, and the kernel given to the verb is the local
fallback:

```cpp
        auto a = cluster.share(runtime.buffer<float>(count).value());
        auto b = cluster.share(runtime.buffer<float>(count).value());
        auto sum = cluster.share(runtime.buffer<float>(count).value());

        const float* a_data = a.data();
        const float* b_data = b.data();
        float* sum_data = sum.data();
```

Source: `applications/examples/distributed_demo.cpp`

```cpp
        // The node names its operation; a worker runs the kernel registered for the same id.
        NodeOptions vector_add;
        vector_add.operation = DistributedDemo::K_VECTOR_ADD;

        auto graph = runtime.graph();
        graph.add(count, Reads(a, b), Writes(sum),
                  [a_data, b_data, sum_data](std::size_t index)
                  {
                      DistributedDemo::add_element(a_data, b_data, sum_data, index);
                  },
                  vector_add);
        graph.run().value();
```

Source: `applications/examples/distributed_demo.cpp`

The lambda runs when the task stays on the master, so it must compute what the kernel computes.
The demo keeps the two equal by calling `add_element()` from both.

`record_registered_task()` (`registered_task.hpp`) removes the second statement of the work: it
takes a `KernelRegistry` and records a task that runs the registered kernel on this machine, or
on a worker once a cluster is attached. It records through a `Recorder&`, so a graph calls it
inside `extend()`; `tests/extension/distributed/test_distributed.cpp` shows the call.

## 7. Share buffers

A buffer can cross only after you share it. `Cluster::share(buffer)` shares one buffer and
`Cluster::share(state)` shares both allocations of a stepped field; `share(address, bytes)`
shares USM you own. `share` is a member of the handle `attach` returned, so a buffer cannot be
shared before a cluster exists. The handle must be `Residency::Shared` or `Residency::Host`: the
master reads the allocation on the host, and a device-resident handle stops the process. The
first block of §6 shares three buffers.

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

At dispatch the master asks an `OffloadPolicy` which worker gets the task, or none. The default,
`LeastLoadedOffloadPolicy`, keeps a task whose inputs total under 64 KiB on the master and
otherwise picks the worker with the lowest estimated cost. [the module README](../../modules/extension/distributed/README.md),
section "offload_policy.hpp", has the estimate and which workers are offered.

To send every task whatever its size, pass a policy with a threshold of 0:

```cpp
        options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
```

Source: `tests/extension/distributed/test_distributed.cpp`

To write your own, implement `OffloadPolicy::pick_worker` and return the `worker_id` of one of
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
| `declined` | Dispatches of an offloadable task that ran on the master instead: the policy picked no worker, no worker had room, the task had used its two attempts, a buffer had been released, or the send was refused |
| `completed_remote` | Tasks a worker finished with every output byte back on the master |
| `redispatched` | Tasks taken back from a worker and resubmitted: the worker was lost, it reported a failure, the task was reaped, or the master stopped |
| `stale_writebacks` | Output chunks the master refused, because their task was not out on a worker or their version was old |
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
| `workers` | One `WorkerSnapshot` per alive worker |

A `WorkerSnapshot` holds the worker's `id`, `name` and `endpoint`, its `inflight` count, the
`rtt_micros` of its last ping, the `free_mem` of its last pong, the `queue_depth` and
`cache_bytes` of its last heartbeat, and `ms_since_last_frame`.

In a healthy run `completed_remote` follows `offloaded` and the reap counters stay at 0.
`declined` above 0 with a worker connected usually means tasks under the 64 KiB threshold.
`tasks_reaped_unacknowledged` or `tasks_reaped_lost_result` above 0 means frames went missing
between the two machines.

`cluster.offloaded()`, `declined()`, `redispatched()`, `stale_writebacks()` and
`alive_workers()` return single values without building the worker list.

A worker logs its own counters once, when it stops, in a line that starts `worker stopped:`.

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
moves. Two timers watch for it. The liveness timeout closes a worker that has also stopped
sending, with the two lines above. `write_idle_timeout` (5 s) closes a peer whose socket takes
no byte for that long, even while frames still arrive from it; the cause in the log is then
`write stalled (no byte left for 5000 ms)`.

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
192.168.1.11:50312 (peer 3) speaks protocol v2, this master v3; build both from one commit; closed, 0 tasks returned to the scheduler
```

The worker logs this and exits with code 4:

```
master (peer 1) speaks protocol v3, this worker v2; build both from one commit; not reconnecting
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

The wire protocol is version 3, and a peer of another version is refused. Build the master and
every worker from one commit; `--version` on a worker prints the version it speaks.
[The module README](../../modules/extension/distributed/README.md) has the frame layout.

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

Defects that are recorded and not yet fixed are in
[`KNOWN_ISSUES.md`](../reference/KNOWN_ISSUES.md).

## 13. Troubleshooting

Find the line you see. The second column says which side shows it.

| Line | Shown by | Cause and what to do |
| --- | --- | --- |
| `Cluster::attach: the transport can be reached from other machines and ClusterOptions::secret is empty` | master, as the message of the error `attach` returns | Load a secret with `SharedSecret::from_file()`, or bind to `127.0.0.1` |
| `Cluster::attach: the master did not start: cannot listen on <address>:<port>: ...` | master, as the message of the error `attach` returns | Another process holds the port, or the address is not one of this machine's. Choose another port or stop the other process |
| `cannot reach master <host>:<port> ...; attempt N, next in M ms` | worker | The master is not running yet, the address or port is wrong, or a firewall drops the connection. A master bound to `127.0.0.1` cannot be reached from another machine: bind it to `0.0.0.0` and give it a secret |
| `<address> (peer N) closed before it registered: closed by the peer` | master | Most often a worker with another secret, or with none; read the worker's log (§10). A port probe looks the same |
| `master (peer N) did not prove the cluster secret` | worker, which exits 3 | The two key files differ. Copy the master's file to the worker again and compare checksums |
| `<address> (peer N) speaks protocol vX, this master vY` | master | The two programs were built from different commits. Rebuild both from one |
| `<address> refused: 64 peers are open, the limit` | master | Every slot is taken. Raise `TCPMasterOptions::max_peers`, or find what holds the connections. The worker logs `master (peer N) takes no more workers` and tries again |
| `peer N sent no valid handshake in 5000 ms; closed` | master | Something connected and said nothing the master understands: a probe, or a worker too slow to answer. For the second, raise `handshake_timeout` |
| `N of M op-tagged nodes will run locally: ...` | master | The counts say why. `no live worker for the op`: no registered worker has that kernel, so check the name on both sides and wait for the worker before the first run. `buffer not shared`: pass every buffer the task touches to `cluster.share()`. `buffer written twice`: the task lists one buffer twice among its writes |
| No line: `declined` rises and `offloaded` stays 0 with a worker connected | master's statistics | The task's inputs total under 64 KiB, which the default policy keeps local (§8) |
| `worker N sent no frame for more than 5000 ms` | master | The worker hung, or the link dropped packets without closing. If the workers are slow and healthy, raise `liveness_timeout` |
| `task T taken back from worker N: no result within the task deadline` | master | The kernel runs longer than `task_deadline`. Raise it, or set it to 0 for no bound |
| `task T: input buffer B version V did not arrive` | worker | The pull went quiet for the pull idle timeout. Check the link; on a slow one raise `--pull-idle-timeout-ms` |
| `task T: cannot allocate N bytes for buffer B (budget X, in use Y)` | worker | The buffers of one task do not fit the cache. Raise `--cache-bytes` |
