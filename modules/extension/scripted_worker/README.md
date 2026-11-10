# scripted_worker

The in-memory adapter of the worker side of the `TransportEvents` port: a worker endpoint that
registers with a master on an `InProcessFabric`, speaks the wire protocol itself and runs no
kernel. It is built with the distributed layer, under `SR_ENABLE_DISTRIBUTED`, is header-only and
internal, so it is never installed; the tests of `distributed` link it as the fake of a worker that
misbehaves on purpose. Every name it declares is in `SushiRuntime::Distributed`.

It depends on `core`, `machine`, `transport`, `wire`, `frame_transport`, `link_security` and
`in_process_transport` through its headers.

## Public entry points

| Header | Holds |
| --- | --- |
| `scripted_frames.hpp` | `ScriptedFrames`, `output_chunk_frame()`, `success_frame()`, `whole_result()` |
| `scripted_worker.hpp` | `ScriptedWorker`, `DispatchAnswer` |

## scripted_worker.hpp

### ScriptedWorker

Registers with a master as a worker that claims the kernel of one operation. It sends `HELLO`,
answers `CHALLENGE` with a `PROOF` under the empty secret and `PING` with `PONG`, so the master
counts it alive. `DispatchAnswer` says what it does with a `TASK_DISPATCH`: `Accept` confirms it and
holds the task until the test answers, `Fail` confirms it and reports that the kernel failed, and
`FallSilent` sends nothing from the dispatch on, not even `PONG`. Its transport does not reconnect.
`report()` sets the facts of its `HELLO` (without it the name is `scripted-worker` and the rest is
empty) and `limit_jobs()` the task limit of its `PROOF`, both before `start()`.
`wait_for_dispatches()` and `dispatch()` read the dispatches that arrived. `send_stream()` hands the
master a source of frames, because a frame that carries the last-chunk flag can only travel in a
stream. A master frame it cannot decode is counted in `undecodable_frames()`, not reported as a
failure, because the adapter lives outside a test. It is a test double, not a worker that runs a
kernel: `WorkerAgent` is that.

## scripted_frames.hpp

`ScriptedFrames` yields a fixed list of frames as one stream, in order, the last as the final frame.
`output_chunk_frame()` makes the frame of one output chunk that carries a whole buffer,
`success_frame()` the frame of a result that reports a task as succeeded, and `whole_result()` one
zeroed chunk per output of a dispatch followed by `success_frame()`.
