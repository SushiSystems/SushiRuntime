# cluster_monitor

The module holds the master's monitor: the thread that pings workers, closes silent peers and
reaps overdue tasks, and the pure step that decides each pass. It is an internal module: its
headers reach the modules that name it and the tests, and are never installed. It is built with
the distributed layer, under `SR_ENABLE_DISTRIBUTED`, and every name it declares is in
`SushiRuntime::Distributed`.

It depends on `core`, `log`, `transport`, `buffer_exchange`, `pending_tasks` and
`offload_coordination`, and its source on `task`, `wire` and `frame_transport`.

## Public entry points

| Header | Holds |
| --- | --- |
| `cluster_monitor_step.hpp` | `plan_pass()`, `pass_period()`, `next_pass_at()`, `reap_cause_words()`, `MonitorFindings`, `MonitorActions`, `MonitorState` |
| `cluster_monitor.hpp` | `ClusterMonitor`, `ClusterMonitorOptions`, `ClusterMonitorStatistics` |
| `unsent_frame_log.hpp` | `Detail::log_unsent_frame()`, `Detail::send_status_text()` |
| `cluster_monitor_log_category.hpp` | `K_CLUSTER_MONITOR_LOG_CATEGORY` |

## cluster_monitor_step.hpp

The decision of a pass is a function of its findings, its state and the time. `plan_pass()` takes
the peers the directory called silent, the peers past the handshake limit and the tasks the table
took back, and returns the actions: a `GOODBYE` and a close for each silent worker, a close for
each late handshake, the reaped tasks counted by cause, the first reaped task of each worker in a
second kept for the log, and whether the held nodes are offered again (after any reap, since the
reaped tasks freed slots). `pass_period()` is the smaller of the configured period (250 ms) and a
quarter of the tightest timeout in force (the liveness timeout, or the task deadline when it is not
0), and at least 1 ms. `next_pass_at()` is the earliest of a period away, the earliest held wait and
the earliest task deadline. None of them reads a clock or starts a thread.

## cluster_monitor.hpp

### A pass

`ClusterMonitor` is the shell over the step. It reads the `Clock` port it is given, so a test moves
a `ManualClock` and calls `run_pass()` instead of waiting for the thread.

1. `PING` to every alive worker; the token is the clock in microseconds.
2. `take_silent()`, `take_handshake_overdue()` and `take_overdue()` gather the findings at one
   instant, and `plan_pass()` decides.
3. Each silent worker gets `GOODBYE(liveness)` and `close_peer()`; the transport then reports the
   loss and the coordinator returns the worker's tasks. Each late peer is closed.
4. The reaped tasks are counted, then `resubmit_tasks()` returns them. A WARN names the task and
   the cause, at most one per worker per second.
5. `HeldNodes::take_expired()`, then `reoffer_nodes()`: a node whose `Wait` bound passed is
   offered again, and its rule then runs it on the master.

The monitor's mutex guards only its sleep. An exception out of a pass is logged and the next pass
runs; tasks a failed `take_overdue()` left in the monitor's list are resubmitted by that next pass.

### Period

`period()` applies `pass_period()` to the options and the timeouts in force.
`set_liveness_timeout()` and `set_task_deadline()` wake the thread, so a shorter timeout applies
from the next pass. The sleep also ends at `HeldNodes::earliest()`, and `wake_by()` wakes the thread
when a node is held whose wait ends before the next pass; the coordinator calls it after each
`try_offload()` that answers true, which costs two atomic loads when nothing is held. `WELCOME`
carries the period a worker registered under.

## unsent_frame_log.hpp

`Detail::log_unsent_frame()` writes one DEBUG when a control frame was not queued, and nothing for
a queued one. The shell calls it for each `PING` and `GOODBYE`.
