# profile

SushiRuntime's front over SushiProfiler: the compiled level of a translation unit and the macros
the runtime writes its zones, values and counters with. The module is header-only and internal; its
headers are for the other modules of the library and are not installed. It is the only code below
the `surface` tier that includes a SushiProfiler header.

It depends on the C++ standard library and on SushiProfiler (`SushiProfiler::SushiProfiler`), which
`profile.hpp` includes when `SR_ENABLE_PROFILER` is on. The build generates `SushiRuntime/profile/profile_config.hpp`
from `profile_config.hpp.in`, so a change of the level rebuilds the files that use a zone and no others. With the option off the macros expand to
nothing, the library builds without SushiProfiler present, and the `profiling` module is not built.

## Public entry points

`profile_config.hpp`, generated from `profile_config.hpp.in`, and `profile_gate.hpp`, which includes it:

| Name | What it does |
| --- | --- |
| `SR_ENABLE_PROFILER` | 1 or 0 as the build chose; 0 compiles every macro out. A translation unit may state it before the include |
| `SR_PROFILE_LEVEL` | The deepest zone level compiled in, as the build chose: 0 none, 1 stages, 2 fine zones, 3 per-element zones |
| `Internal::Profile::K_COMPILED_LEVEL` | The level of this translation unit; 0 when the profiler is off. It has internal linkage, so two units compiled at different levels do not meet |

`profile.hpp`, which includes the gate:

| Name | What it does |
| --- | --- |
| `SR_ZONE(name)` | Records the rest of the scope as a stage zone, level 1, in the group `Runtime` |
| `SR_ZONE_FINE(name)` | The same at level 2; the per-dispatch zones |
| `SR_ZONE_DETAIL(name)` | The same at level 3; the work-stealing search |
| `SR_VALUE(name, unit, value)` | Records a sample, level 1 |
| `SR_COUNTER(name, unit, op, value)` | Records a value folded per period by the counter operation `op` (`Add`, `Set`, `Max`, `Min`), level 1 |
| `SR_INSTANT(name)` | Records an occurrence, level 1 |
| `SR_LOCK_WAIT(name, mutex)` | Locks `mutex` for the rest of the scope and records a zone only when the lock was contended |
| `Internal::Profile::current_thread_index()` | The profiler's index of the calling thread, or 0 when it has none |
| `Internal::Profile::name_this_thread(name)` | Registers the calling thread and names it, so its first zone allocates nothing |

`profile_keys.hpp`: `K_DEVICE_SOURCE_NAME` (`sushiruntime.device`), `K_HOST_TRACK_NAME` (`host`),
`K_KEY_LAYER` (`layer`), `K_LAYER_SCHEDULER`, `K_LAYER_CHAIN` and `K_ALL_KEYS`, which lists each
once. The attribute keys `node`, `invocation`, `operation`, `launched_width`, `live_width`, `flops`,
`bytes` and `dtype` are SushiProfiler's (`SushiProfiler::DeviceSpanKeys`), and the `profiling` module
uses them from there.

## What the macros cost

A zone belongs in an exported `.cpp`, never in a header template: a template compiles in the
consumer, with a site per translation unit and at the consumer's level. With the profiler on, a
zone at a compiled-in level costs one relaxed load and a branch while no session records, and
under 25 ns while one does (the profiler's budget, not a measurement of this runtime). A zone above
the compiled level is an empty object and registers no site.

With `SR_ENABLE_PROFILER` off, `SR_VALUE` and `SR_COUNTER` evaluate nothing, since their operand
is only named in `sizeof`, and `SR_LOCK_WAIT` is a plain `std::lock_guard`. The file that uses it
includes `<mutex>`; the header does not.

## Tests

`tests/foundation/profile/` builds `sr_profile_test_objects`, linked into `sr_foundation_tests`; its `README.md` describes it.

## Designs

<!-- designs:begin -->
- [PROFILING](../../../docs/design/PROFILING/README.md): How SushiRuntime reports its own time to SushiProfiler and to any observer, and which timing leaves its public report.
<!-- designs:end -->
