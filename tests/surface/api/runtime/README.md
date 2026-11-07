# api tests: runtime

The tests of `Runtime`, its configuration, its errors and its logging controls. They are part of `sr_surface_tests`;
[the page of the module's tests](../README.md) says how to run them.

A `Small_` test takes a host runtime of its own from the fixture base `TestSupport::HostRuntimeTest`, or
makes one with `Runtime::create_host()` where the configuration it passes is the promise, such as a
device count, a node name or labels. A `Medium_` test takes the program's runtime from the fixture
base `TestSupport::RuntimeTest`; it stays medium where the promise is what a real device, its queue,
the worker pool, the host name or a `RuntimeConfig` passed to `Runtime::create()` does. A test that
sets state on the shared runtime, such as the placement rules, puts it back in `SetUp` or
`TearDown`. The host runtime has no rebalancer, worker count or device filter, so the tests of those
stay medium.

## test_advanced_controls.cpp

The file covers the rebalancer control: the rebalancer is off unless requested, and a run gives the
same result with it off and on.

The default is the assertion that matters. The background thermal rebalancer migrates tasks between
workers on a 5 ms heartbeat. That changes which worker folds which tile and puts a periodic wakeup
under every frame, a throughput trade that gains a simulation frame nothing.

| Test | What it proves |
| --- | --- |
| `RebalancerIsOffByDefault` | The program's runtime, built with no config, does not run the rebalancer. |
| `RebalancerIsOnWhenRequested` | `RuntimeConfig::rebalancer` starts it. |
| `RebalancerToggleKeepsExecutionCorrect` | Rows of off and on: the flag `set_rebalancer_enabled()` set is the one `is_rebalancer_enabled()` reports, and the same graph gives the same result. |

## test_errors.cpp

A task exception must abort only its own run and must not kill the worker pool. The run that threw
surfaces the error to the caller, and a fresh graph on the same `Runtime` must still run and produce
correct results afterwards. The defect this file guards against latched `shutdown_requested` on a
single throw and left the pool dead for good.

| Test | What it proves |
| --- | --- |
| `ThrowingNodeIsReportedByNameAndRegistrationIndex` | The error of `run()` names the throwing node and its registration index and holds the exception text, and it does not name a healthy node. |
| `AGraphRunsCleanAfterAKernelThrew` | The command-group function of the first graph throws, so `run()` returns the error and does not hang; a second graph on the same runtime then writes its output. |

## test_placement_scope.cpp

The file covers `PlacementScope`, the two overloads of `Runtime::placement_scope()` and the device
a scope resolves to. The device tests send a class to `advanced().device_count()`, one past the last
device, which no machine has, so the absent-device cases run on any machine. They run small on a host
runtime of their own, which starts with no rules; a runtime built with labels or a second device is
made in the test body with `Runtime::create_host()`. The two medium tests run on the program's
runtime, so the fixture of the memory test clears the rules before and after it.

| Test | What it proves |
| --- | --- |
| `AScopeWithoutARuntimeGivesDeviceZero`, `AScopeWithoutARuntimeRefusesABuffer` | A scope built from a class and a key has no rules to read: `device()` gives 0 and `buffer()` returns `InvalidState`. |
| `TheStringAndClassOverloadsGiveEqualClasses`, `BothOverloadsKeepAKeyOfZeroAndANoneKey`, `ANameWithoutADotIsAClass` | The string and the `PlacementClass` overloads build equal classes for one name and keep a key of 0 and a none key. A name with no dot is a class. |
| `AMalformedNameStopsTheProcess` (medium) | Rows of an empty name, a name that starts or ends with `.` and a name with two `.` in a row: each stops the process. |
| `ANoRuleScopeGivesDeviceZero`, `ARuleForAnotherClassLeavesDeviceZero` | No rule, or a rule for another class, gives device 0. |
| `ARuleWithAPresentTargetGivesDeviceZero` | Rows of a machine with no device part, an index and a kind the host runtime has: each gives device 0. |
| `AtLeastOneByteOfMemoryGivesDeviceZero` (medium) | A rule asking for one byte of device memory gives device 0 on a real device. A host device reports no memory, so this row stays on the program's runtime. |
| `LocalGivesDeviceZeroForAnAbsentDevice`, `FailNamesTheRuleAndTheDevice`, `WaitGivesDeviceZeroForAnAbsentDevice` | A rule for an absent device gives device 0 under `Local`, an `InvalidArgument` that holds the rule's name and `device index N` under `Fail`, and device 0 under `Wait`, since a local device never appears. The `Wait` test reads no clock. |
| `AnotherMachinesNameMakesTheTargetAbsent`, `ALabelNoMachineCarriesMakesTheTargetAbsent`, `LocalFallsBackWhenTheMachineIsAnotherOne` | A machine name or label that is not this machine's makes the target absent, and `Local` falls back to device 0. How a machine part and a device part match is held by `tests/foundation/placement/test_placement_target_matching.cpp`. |
| `ALabelTheMachineCarriesMakesTheTargetPresent` | A label the runtime's config gave makes the target present. |
| `AScopeOpenedBeforeARefusingSetIsRefusedByIt`, `AScopeOpenedBeforeTheRulesAreClearedIsNoLongerRefused` | A scope made before `set_placement_rules()` is refused by a `Fail` set, and accepted again after `clear_placement_rules()`. |
| `LocalPlacesTheBufferOnDeviceZero`, `FailRefusesTheBuffer`, `FailAllocatesNothing`, `ABufferLandsOnTheDeviceTheRuleNames` | `buffer<T>()` allocates on device 0 under `Local`, returns the error of `device()` and allocates nothing under `Fail`, and allocates on device 1 when a rule names it, on a host runtime of two devices. |

The resolution of a rule against the machine facts is decided on values and could run small. It sits in
`modules/surface/api/source/local_rule_device.hpp` and an anonymous function of `placement_scope.cpp`,
neither of which a test reaches; RW-110 holds moving it.

## test_runtime.cpp

The file covers `Runtime::create()`, `RuntimeConfig` and the `AdvancedControls` view. Four
`static_assert` lines at the top state that a `Runtime` moves without throwing and does not copy, so
a copy constructor that returns fails the compile and not a test.

| Test | What it proves |
| --- | --- |
| `TheWorkerCountOfTheConfigReachesThePool`, `ThePinningPolicyOfTheConfigReachesThePool` (medium) | A `RuntimeConfig` with `worker_count` 2, and one with `pinning` `Float`, give a pool with that count and that policy. The rebalancer reaches the runtime in `RebalancerIsOnWhenRequested`; `force_scheduler` has no public reader, and RW-114 holds it. |
| `TheViewOutlivesTheRuntime` | A view taken while the `Runtime` lives answers with the same device count after the `Runtime` is destroyed. A view that held a reference would read a destroyed context, which AddressSanitizer reports. |
| `DevicesListsOneEntryPerDevice`, `EntryNamesTheDeviceBehindItsQueue` (medium) | `devices()` holds one entry per device, and rows of entry `i` carry the name of the SYCL device behind the queue of `DeviceIndex{i}`. |
| `ABufferLandsOnTheFirstDevice` | A buffer allocated with no `DeviceIndex` is on the first entry of `devices()`. |
| `TopologyNamesTheMachineAsTheConfigDoes`, `TopologyReportsTheOperatingSystem`, `TopologyListsTheRuntimesDevices`, `TopologyCarriesTheConfiguredLabelsInOrder` | `topology()` reports the name and labels the config gave, in order, the operating system the test was built for, and one `DeviceFacts` per entry of `advanced().devices()` with the same kind, name and memory. |
| `TopologyNamesTheMachineByItsHostNameWhenTheConfigGivesNone` (medium) | A config with no `node_name` gives the name the operating system reports for the host, read with `GetComputerNameExA` or `gethostname`. The host runtime never reads the host name. |
| `TopologyCarriesNoLabelWhenTheConfigGivesNone` | A config with no labels gives an empty list. |
| `CreateRefusesALabelKeyThatBreaksARule` | Rows of an empty key, a key holding `=` and a repeated key: `create_host()` returns `ErrorCode::InvalidArgument` with a message that names `RuntimeConfig::labels` and the position or the key of the label. |
| `AMovedRuntimeKeepsTheContextItsViewsShare` | A view taken before a move sees the allocation made on the moved-to `Runtime`. |
| `AHandleItHandsOutReachesItsOwnContext` | An allocation made on the Runtime, and its release, are read back through the `AdvancedControls` it handed out, so the handle shares the Runtime's own context. This is the public test of `ContextHandleAccess::handle_of`. |
| `ACallerWaitsAndCopiesThroughTheDeviceQueue` (medium) | A kernel submitted on the queue `advanced().queue()` returns is finished when `Runtime::wait()` returns, and a `memcpy` through the same queue brings the values to a host vector. |
| `AQueueOfAnUnknownDeviceStops` (medium) | `advanced().queue()` asked for the device after the last one stops the process. |
| `ABufferNoDeviceCanHoldReturnsAllocationFailed` (medium) | `buffer()` asked for half the address space in bytes on the machine's devices returns `AllocationFailed`, and the message holds the element count. |
| `AStateNoDeviceCanHoldReturnsAllocationFailed`, `AFailedAllocationLeavesTheRuntimeUsable` | `state()` asked for half the address space in bytes returns `AllocationFailed` on a host runtime, and a buffer of 16 bytes is granted afterwards. The memory module has no seam that makes an allocation fail. |
| `ABufferAddsOneLiveAllocation`, `AStateAddsTwoLiveAllocations`, `FreeingABufferAndAStateReturnsTheLiveAllocationCount` | `advanced().live_allocation_count()` rises by one for a buffer and by two for a state, and is back at its first value when both leave their scope. |

### Placement rules

`Small_RuntimePlacementRules` runs on a host runtime of its own; `Medium_RuntimePlacementRules` holds the race
test on the shared runtime and clears the rules before and after it. One graph holds a node of class `sushiai.replica` with key 0 and a node with no class.

| Test | What it proves |
| --- | --- |
| `ARunWithNoRulesReportsNoWinner` | With no rules the plan reports no winner for either node. |
| `ANodeWithNoClassMatchesNoRuleThatStatesOne` | After a set with one exact rule, the plan reports rule 0 for the first node and none for the second. |
| `ANewRuleSetRecompilesThePlan` | The run after `set_placement_rules()` rebuilds the plan. |
| `ASecondCompileReportsTheWinnerOfTheNewRules` | After a set that adds a prefix rule `sushiai.*` of higher priority, the plan reports rule 1. |
| `AnUnchangedRuleSetDoesNotRebuildThePlan` | A run with the same rules leaves `compile_count()` where it was. |
| `ClearingTheRulesReportsNoWinner` | `clear_placement_rules()` makes the next plan report none. |
| `SettingTheRulesDuringARunIsRefusedAsInvalidState`, `ClearingTheRulesDuringARunIsRefusedAsInvalidState`, `TheRulesCanBeChangedOnceTheRunEnded` | A host node stays queued in the executor while the run `run_async()` started is in flight; `set_placement_rules()` and `clear_placement_rules()` called then each return `ErrorCode::InvalidState`, the first with a message that says to wait, and both succeed after the run ends. The test drains the executor by waiting, with no flag and no deadline. |
| `Regression_ARuleChangeRacingARunStartNeverLandsInsideTheRun` (medium) | A writer thread alternates a refusing set and a cleared set while 2000 runs start and two threads read a scope. A host node asks a scope for its device twice, a yield apart, and the answers never differ: a change is refused while a run is in flight. |

### Logging

The last tests of the file cover what a program sees of the logger when it creates a `Runtime`.
They are here and not in the `log` module because the lines they look for are logged by
`device` and `topology` while `Runtime::create()` runs.

`Small_LoggingDefaults` and `Medium_LoggingDefaults` put a sink that keeps the records it receives on the
logger, and afterwards restore the default sink and the level the process had.

| Test | What it proves |
| --- | --- |
| `CreateIsSilentAtTheDefaultLevel` | At `Warn` no record reaches the sink while a host `Runtime` is created and destroyed. The test sets `Warn` itself, since `SR_LOG_LEVEL` in the environment of the test run would otherwise decide the level. |
| `DeviceLineArrivesThroughTheSinkAtInfo` (medium) | At `Info` the sink receives the `Device: ...` line of `device_queue_set.cpp` at level `Info`, which shows the sink sees what `Runtime::create()` logs. |

### `AThreadOutlivingTheRuntimeTearsDownCleanly`

A thread that allocated through a `Runtime` and is still alive when that `Runtime` is destroyed
unwinds after it. A long-running simulation process hits this shape when a worker outlives the
context it allocated from.

The risk is thread-local state that outlives what it points into. Epoch reclamation keeps a slot per
thread, and a thread that reaches its exit-time destructors after the `Runtime` is gone must not
touch freed memory.

The thread allocates and fills a buffer, so that its exit-time destructors have touched the
per-thread bookkeeping of the runtime. It runs those destructors when the test joins it. The thread
sets a flag after the `Runtime` is gone and the test asserts the flag; under AddressSanitizer a
use-after-free is also a failure, and without it the defect still crashes often enough to fail the
test. The test builds its own `Runtime` since it destroys it.

## test_runtime_config.cpp

The file covers the co-tenancy contract: the runtime is a tenant of the machine, not its owner.

Inside a game the runtime shares the machine with a main thread, a render thread, an audio callback
and asset I/O. A batch runtime takes every core and pins worker 0 onto the core the host's main
thread runs on. A host has to be able to keep some cores for itself. These tests assert the
mechanism that allows it (a worker count, a core range, and the option not to pin at all) and check
that a narrower pool still executes correctly.

| Test | What it proves |
| --- | --- |
| `DefaultsToOneWorkerPerCoreFromCoreZero` | The default is the sole-tenant behaviour: one worker per logical core, starting at core 0. A silent change to the default would move the performance profile of every existing consumer. |
| `ReservesTheCoresTheHostAskedFor` | A host that wants cores 0 to `reserved - 1` for its own threads asks for a pool that starts past them. Both the count and the placement must be exactly what the host requested. The test skips below four cores. |
| `ASingleWorkerStillExecutesTheWholeGraph` | A pool much smaller than the machine still runs everything correctly. |
| `FloatPolicyRunsUnpinned` | The `Float` policy leaves placement to the operating system: the pool is the requested size and no worker is bound. That is the right choice inside a container whose CPU allowance is enforced by quota and not by affinity. |
| `AnOutOfRangeCoreRangeIsClampedNotIgnored` | A core range that runs off the end of the machine is clamped. The operating system's affinity calls accept an out-of-range mask and then do not bind, so without the clamp the runtime would report a reservation it never made. |

The pool shape is decided on the core count and the config. It sits in `RuntimeContext` and the
worker pool, which no test calls with a count of its own; RW-110 holds a seam for it.

### The device filter

Both filter properties move a failure earlier. A double-precision kernel on a device without
`sycl::aspect::fp64` fails inside the driver at launch, with a message that names neither the device
nor the aspect; `require_fp64` moves that failure to discovery. A filter that matches nothing would
otherwise surface as "Queue index out of bounds" from the first allocator call, which is the first
thing a player on a machine with no SYCL device would see.

| Test | What it proves |
| --- | --- |
| `ARuntimeWithNoConfigDrivesADeviceOfEveryClassTheMachineHas` | Rows of CPU, GPU and accelerator: a runtime made with the default `DeviceFilter`, which accepts every class, lists a device of each class the machine has. A row for a class the machine lacks skips. |
| `RequireFp64AdmitsOnlyFp64Devices` | Rows of device index: every device a `require_fp64` runtime drives reports fp64 on the queue behind it. The fixture skips on a lane with no fp64 device. |
| `StillConstructsOnAConformingLane` | A lane that has an fp64 device still builds a runtime with `require_fp64` set. The CPU lanes this suite runs on all report fp64. |
| `IsRefusedWithUnsupported` | On a lane with no fp64 device, `Runtime::create()` returns `Unsupported` and not a driver error from inside a kernel launch. The fixture skips where a device reports fp64. |
| `AnEmptyFilterFailsWithUnsupported` | A filter that admits nothing fails at construction with `ErrorCode::Unsupported`, and the message names the filter, so the reader can tell a machine that has nothing from a filter that excluded everything. |
