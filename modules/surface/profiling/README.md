# profiling

The runtime's part of a SushiProfiler profile: a source that reports the runtime's device work as
spans, a contributor that states the machine and the build as facts, and the function a host calls
to register both. The module is public: its headers are installed, and it is built only when
`SR_ENABLE_PROFILER` is on.

It depends on `core`, `api` (for `Runtime`), `report`, `profile`, `log`, `machine` and SushiProfiler
(`SushiProfiler::SushiProfiler`), whose headers `register_profiler_parts.hpp` includes. Its sources
also use `execution`, `context_access`, `device` and `sycl`.

## Public entry points

| Header | Holds |
| --- | --- |
| `register_profiler_parts.hpp` | `RuntimeProfilerParts` and `register_profiler_parts()`: what a host calls to register the runtime |
| `profiled_runtime.hpp` | `ProfiledRuntime`, what the registration reads of one runtime, and `profiled_runtime_of()`, which wraps a `Runtime` in it |
| `in_memory_profiled_runtime.hpp` | `InMemoryProfiledRuntime`, the `ProfiledRuntime` that answers with stored values |
| `source_names.hpp` | `K_DEVICE_SOURCE_NAME` (`sushiruntime.device`), the name the source is registered under; it includes only `<string_view>` |
| `device_track_name.hpp` | `device_track_name()` and `host_track_name()`: the track names the source writes |
| `device_source.hpp` | `DeviceSource`, the `SushiProfiler::Source`, and `FeedAttachment` |
| `run_queue.hpp` | `RunQueue`: the bounded hand-over of observations to the consumer thread |
| `run_feed.hpp` | `RunFeed`: the observer that puts one runtime's runs on the queue |
| `clock_conversion.hpp` | `to_steady_ns()` and `saturate_uncertainty()` |
| `runtime_environment.hpp` | `RuntimeEnvironment`, the environment contributor |
| `profiling_log_category.hpp` | `K_PROFILING_LOG_CATEGORY` |

The first four are what a host or another library uses. The others are declared in
`SushiRuntime::Internal::Profiling` and belong to the module; they are in `include/` so a test reaches
them.

## How a host registers the runtime

```cpp
SushiProfiler::Registry registry;
SushiProfiler::register_builtin_writers(registry, std::cout);
SushiRuntime::RuntimeProfilerParts parts = SushiRuntime::register_profiler_parts(registry, runtime);
// start a SushiProfiler session with the registry; stop it before `parts` goes
```

Source: not compiled

`register_profiler_parts()` follows the pattern of SushiProfiler's client library guide. It has a second form,
`register_profiler_parts(registry, std::shared_ptr<ProfiledRuntime>)`, and the `Runtime` form wraps its runtime with
`profiled_runtime_of()` and forwards to it, as the `RuntimeProfilerParts` constructor and `attach()` do. A host that
has no `Runtime`, such as a test of the registration, passes an `InMemoryProfiledRuntime`. The parts
object owns what the registry only refers to: the queue the source reads and the contributor. It is
neither copyable nor movable, and the host keeps it alive until every session started with the
registry has stopped. The registry refuses a source name twice; then `is_registered()` is false and
the object added nothing.

The runtime and the host link the same SushiProfiler target, the shared library. A runtime on the
static target would record into a collector the host's session never drains.

One source serves every runtime of the process. `RuntimeProfilerParts::attach()` adds a further
runtime to the same source before a session starts, and returns false while a session runs; it too takes a `Runtime`
or a `ProfiledRuntime`. The
tracks name the runtime's node and the device, so two runtimes land on one timeline.

## What a session does to the runtime

When a session begins, the source adds one observer to each attached runtime; when it ends, and in
the destructor of the parts object, the observers are removed, each through the handle it was added
with and only the ones this object added. The observers are added behind the profiler's `Collector::sources_wanted` gate, so a session that is paused or not
recording costs the runtime no device timing, and a runtime with an observer of another owner keeps timing on
for it. While a gate is open or an ungated observer is added every run of the runtime is timed, chosen per run when it is armed, so no plan is
rebuilt when the session begins or ends: the run that starts after `begin()` carries records, and a run
already in flight reaches the source with none. A runtime that already has an observer of another owner keeps it; every observer
receives every run. The parts object keeps the context of each runtime alive until it is destroyed.

## What the source writes

The source is `sushiruntime.device`. For each dispatch of a run it writes one span:

| Span field | From |
| --- | --- |
| `queued_ns`, `start_ns`, `end_ns` | The record's submit, start and end, converted to the steady clock through the pairing of the record's device, then to the record's clock |
| `uncertainty_ns` | The pairing's uncertainty, saturated to 32 bits; 0 for a host span |
| track | `device_track_name()` of the record's device, or `host_track_name()` for a host record |
| name | The node's name in the plan's node table, or `node <index>` when the table has no row |
| `submit_thread` | The record's profiler thread index |

and these attributes, whose keys are `SushiProfiler::DeviceSpanKeys`: `node` (the registration
index), `invocation`, `operation` (the operation's hash, when the node states one),
`launched_width` and `live_width` (each omitted when unknown), `layer` (a name, `scheduler` or
`chain`), `graph` (a name: the label of the graph that owns the plan, when it set one), `origin` (a name:
the caller's own operation the node belongs to, when it stated one), and, when the node's row states a cost, `flops`, `bytes` and `dtype` (a name, omitted
when the dtype is unstated). A stated zero cost is written as zeros and an absent cost writes none
of the three.

### Which period a span belongs to

The runtime reads no period. `collect()` receives the closed period's steady span, and a span goes in
the period whose span holds its converted start: an older one is dropped with `drop_late`, a later one
waits for its period. The per-device pairing is therefore what places a span: a device record with no
pairing in its observation has no start on the steady clock, so it is dropped late and counted. A run
whose work finished after the period's linger is counted in the same way.

The source reports `max_delay_ns()`, 2000 ms (`DeviceSource::K_MAX_DELAY_NS`, below the profiler's limit of
60 000 ms), so the consumer holds a closed period open that long for the source's late work. A span that
finishes within the delay is placed in its period and not dropped late. `drop_late` still counts a span that
arrives after the delay, a span with no pairing, the dispatches a run lost, and the records the source queue
dropped when it was full.

### What is counted as dropped

The record's dropped spans cover the spans the source drops as late, the dispatches an observation
reports as lost (`RunObservation::drops`), the records of observations the queue dropped when it was
full, and the device records with no pairing. The queue holds at most 87 381 events, 4 MiB of 48-byte
records, whatever the number of runs they come from; a push drops the oldest runs until the new run fits,
and a run larger than the bound is kept alone, so memory stays bounded when no session collects. Observations and spans still waiting when
the session ends are discarded, and a `DEBUG` line gives the counts.

The record keeps one total, so the source also counts each loss by cause in a `SpanLossTally`
(`span_loss_tally.hpp`): the five causes of `DropCause`, no clock pairing, queue overflow, a span older
than the period it arrived in, and a span the period's table refused. When any span was lost, a
`WARN` line names the counts since the last one, at most once per 5 s of collected periods.

### Track names

`device_track_name()` returns `<node>/<device index>/<device name>`, and `host_track_name()` returns
`<node>/host`. The node is the machine's name in `NodeFacts`. Every overload of
`device_track_name()` build the string in one place, which the source uses too, so a library that
states a fact keyed by a track calls the function and never rebuilds the name. A device index the
runtime does not have names a device `device`. A third overload takes the node name, the device index and a
`std::vector<DeviceDescription>`, and applies the same fallback; the `Runtime` overload forwards to it.

## The environment contributor

`RuntimeEnvironment` sets these facts when a session starts, compiled inside the runtime library so
the build facts are the library's own and not the host's. It reads each runtime through `ProfiledRuntime`:

| Fact | Holds |
| --- | --- |
| `sushiruntime.version` | The library's version |
| `sushiruntime.<node>.devices` | The device count |
| `sushiruntime.<node>.device.<i>.name`, `.kind`, `.backend`, `.driver`, `.memory_bytes`, `.compute_units`, `.numa_node` | Each device; `driver` reads `unknown` when the backend does not answer |
| `sushiruntime.<node>.workers`, `.rebalancer` | The worker pool and the rebalancer state |
| build: `sushiruntime.build_type`, `.profile_level`, `.sanitizer`, `.sycl_toolchain` | How the library was built; `compare_records` refuses records whose build facts differ |

## Clock conversion

`to_steady_ns(pairing, event_ns)` moves a reading of a device's event clock onto the steady clock by
the pairing's two readings taken beside each other; a time before the steady clock's origin is 0.
`saturate_uncertainty()` clamps the pairing's 64-bit uncertainty to the 32 bits a span carries.

## profiled_runtime.hpp

### ProfiledRuntime

ProfiledRuntime is the port through which `RuntimeProfilerParts` and `RuntimeEnvironment` read one runtime:
its node name, its devices, the driver of a device, the worker count, the rebalancer state, and
`observe_runs()`, which adds a `RunObserver` behind the profiler's gate and returns the handle that removes
it. `profiled_runtime_of(runtime)` returns the adapter over a `Runtime`; it shares ownership of the runtime's
context, and its `driver_of()` returns the device's `DeviceDescription::driver_version`, `unknown` when the backend did not answer and for a host device.

## in_memory_profiled_runtime.hpp

### InMemoryProfiledRuntime

InMemoryProfiledRuntime is the `ProfiledRuntime` that drives no device. A test adds devices with
`add_device()` (each with its driver), sets the workers and the rebalancer with `set_workers()`, and reads
how many observers are added with `observer_count()`. A device with no stored driver reads `unknown`.

## in_memory_feed_attachment.hpp

### InMemoryFeedAttachment

InMemoryFeedAttachment is the `FeedAttachment` of `device_source.hpp` that starts and stops nothing.
It records each `attach()` and `detach()` in order. `attach()` returns the refusal set with
`refuse_with()`; until one is set the refusal is empty, which accepts.

## Tests

`tests/surface/profiling/` builds `sr_profiling_test_objects`, linked into `sr_surface_tests`; its `README.md` describes it.

## Designs

<!-- designs:begin -->
- [PROFILING](../../../docs/design/PROFILING/README.md): How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
<!-- designs:end -->
