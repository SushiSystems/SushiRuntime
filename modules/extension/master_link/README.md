# master_link

The module holds the worker's side of the master connection: the handshake, the heartbeat, the
watch on the master's silence, and the pure step that decides what the link's thread does. It is an
internal module: its headers reach the modules that name it and the tests, and are never installed.
It is built with the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is
in `SushiRuntime::Distributed`.

It depends on `core`, `log`, `machine`, `task`, `transport`, `wire` and `link_security`, and its
source on `frame_transport`.

## Public entry points

| Header | Holds |
| --- | --- |
| `link_tick.hpp` | `plan_link_tick()`, `LinkTickInput`, `LinkTick`, `LinkPhase`, `LinkAction` |
| `master_link.hpp` | `MasterLink`, `MasterLinkOptions`, `LinkChange`, `LinkEnd` |
| `master_link_log_category.hpp` | `K_MASTER_LINK_LOG_CATEGORY` |

## link_tick.hpp

`plan_link_tick()` decides what the thread does at a time, from the state of the link: a link that
ended stops its transport once; a registered link closes a master silent for the liveness timeout
(and waits for the loss), else sends a `HEARTBEAT` when one is due; a link in its handshake sends
`HELLO` again when no `WELCOME` came within one heartbeat interval. It also gives the time of the
next look. It reads no clock and touches no thread.

## master_link.hpp

### The link

`MasterLink` registers with the master on each connection and watches that it stays there. The
`on_*` members are the transport's events, called from the reader thread, and each returns a
`LinkChange` the agent acts on. The link owns one mutex and releases it before it calls the
transport or a probe. It reads the `Clock` port it is given for every interval, so a test moves a
`ManualClock` and calls `step()`, which does what is due on the calling thread and returns when the
next thing is due. `start()` runs the same step on the link's own thread. `LinkEnd` mirrors the agent's
`WorkerEnd`, which the agent maps one to one.

### The handshake

On a connection the link sends `HELLO` with a fresh nonce. A `CHALLENGE` is checked against the
nonces of the last four `HELLO`s, newest first: a proof that matches the newest is answered with
`PROOF`; one that matches an earlier `HELLO` is ignored; one that matches none, while no `HELLO` was
forgotten, ends the link as `RejectedAuthentication`. A `CHALLENGE` that may answer a forgotten
`HELLO` proves nothing and is ignored, so the next `HELLO` runs the handshake again.

### Leaving

After `say_goodbye()` or `stop()` the link is leaving: a loss or an abandoned connect is logged at
DEBUG and changes no end, and a new connection is closed.
