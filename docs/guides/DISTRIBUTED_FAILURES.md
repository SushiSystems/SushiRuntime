# Distributed failures

What the master and the workers do when a worker is lost, goes silent, stalls, loses a task, or
disagrees with the master on the secret or the protocol version, and how each case shows in
the statistics. [The distributed guide](DISTRIBUTED_GUIDE.md) says how to start the layer.

A run that completes leaves the same bytes in the master's buffers as a run with no cluster. The
master takes a task back whenever it cannot be sure of the result, raises the version of the
task's outputs so late bytes from the old attempt are refused, and resubmits the task.

## A worker is lost

The worker's process dies or its connection breaks. The master logs:

```
worker 1 "node-b" 192.168.1.11:50312 lost: closed by the peer; 1 tasks returned to the scheduler
```

The cause is one of `closed by the peer`, `closed by this master`, `read or write failed`,
`write stalled`, `malformed frame` or `a frame handler failed`, followed by the OS reason in
brackets when there is one. The returned tasks go to another worker or run on the master.
`peers_lost` rises by one and `redispatched` by the number of tasks.

## A worker goes silent

A worker that sends no frame for `liveness_timeout` is closed. Every frame counts, including the
chunks of a long transfer, so a slow link is not a silent one. The master logs two lines:

```
worker 1 sent no frame for more than 5000 ms; its connection is closed and its tasks return to the scheduler
worker 1 "node-b" 192.168.1.11:50312 lost: closed by this master; 1 tasks returned to the scheduler
```

The worker, if it is still alive, sees the close, reconnects and registers again.

## A link stalls

A link stalls when the other machine stops taking bytes: the connection stays open and nothing
moves. Two timers watch for it. The liveness timeout closes a worker that has also stopped
sending, with the two lines above. `write_idle_timeout` (5 s) closes a peer whose socket takes
no byte for that long, even while frames still arrive from it; the cause in the log is then
`write stalled (no byte left for 5000 ms)`.

## One task goes missing

The worker is alive and one task never comes back. The master takes it back and logs at most one
such line per worker per second:

```
task 42 taken back from worker 1: no result within the task deadline; it is resubmitted to the scheduler
```

The cause is one of `no result within the task deadline`,
`the worker did not acknowledge the dispatch` or `the worker's heartbeats stopped listing it`.
The last two apply even with `task_deadline` at 0.

## The master is lost

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

## The secrets differ

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

## The versions differ

Master and worker must speak the same protocol version; there is no negotiation. The master
logs this, counts `handshake_rejected_version` and closes:

```
192.168.1.11:50312 (peer 3) speaks protocol v2, this master v3; build both from one commit; closed, 0 tasks returned to the scheduler
```

The worker logs this and exits with code 4:

```
master (peer 1) speaks protocol v4, this worker v3; build both from one commit; not reconnecting
```

Something that is not a SushiRuntime program at all, such as a port scanner, is closed with
`closed before it registered: not a SushiRuntime frame (first four 0x47455420)`.

## The kernel fails on the worker

The worker logs the failure and keeps serving:

```
task 42: the kernel of operation <id> threw: <what()>; reported as kernel failed
```

The master resubmits the task and `redispatched` rises. The same happens when an output does not
fit the worker's cache (`cannot allocate ... bytes for buffer ...`), when an input does not
arrive within the pull idle timeout (`input buffer ... did not arrive`), and when the worker has
no kernel for the operation.
