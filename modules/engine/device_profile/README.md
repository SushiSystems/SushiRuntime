# device_profile

The module measures and keeps what each device can do: the device profile, its file, the cost
model built from it, the probe that measures a device, and the fold of measured dispatches back
into the profiles. It is an internal module: its headers reach the modules
that name it and the tests, and are never installed. Its names are in
`SushiRuntime::Internal::Coherence`.

It depends on `core`, `handle`, `log`, `memory`, `report` and `transfer`.

## Public entry points

| Header | Holds |
| --- | --- |
| `device_profile.hpp` | `DeviceProfile` and `PathProfile`, what one device measured |
| `device_profile_set.hpp` | `DeviceProfileSet`, the profiles by `DeviceKey`, and `has_profile()` |
| `device_profile_file.hpp` | `DeviceProfileFile`, the reader and writer of the profile file |
| `cost_model.hpp` | `CostModel`, which prices a `NodeCost` and a copy in seconds |
| `profile_transfer_cost.hpp` | `select_transfer_cost()` and `SpaceDeviceResolver` |
| `profile_probe.hpp` | `ProfileProbe`, the port that times copies and kernels on a device |
| `profile_measurement.hpp` | `ProfileMeasurement` and `MeasurementPlan`, which build a `DeviceProfile` through the probe |
| `probe_devices.hpp` | `ProbeDevices` and `ProbeDevice`, the port that finds a device's queue and space by `DeviceKey` |
| `device_profile_probe.hpp` | `DeviceProfileProbe` and `ProbeKernel`, the probe that times real SYCL devices |
| `dispatch_observation.hpp` | `DispatchObservation`, `DeviceKeyTable` and `observe_dispatches()`, the ledger's records as measured work |
| `profile_fold.hpp` | `fold_observations()`, which folds measured throughput into the device profiles |
| `profile_update.hpp` | `update_profile_file()`, which folds a run into the profile file and writes it back |
| `device_profile_log_category.hpp` | `K_DEVICE_PROFILE_LOG_CATEGORY`, the log category of the module |

## Device profile

`DeviceProfile` holds, for one `DeviceKey`, the capacity and reserve of the device's pool, its memory
bandwidth, a base throughput, a throughput per `OperationKind` and the bandwidth and latency of each
transfer path that leaves it, by `TransferPathID`. Throughputs are operations per second, bandwidths are bytes
per second and latencies are seconds. A kind with no entry takes the base throughput.

`DeviceProfileSet` holds the profiles in `DeviceKey` order. `has_profile()` is how the partitioner
learns that a device was never measured; such a device receives no node.

## CostModel

A path profile keeps its name for the file and logs, and `DeviceProfile::set_path()` is the one place a
name becomes an identity. `CostModel::transfer_time()` and `ProfileProbe::measure_path_copy()` take the
identity; a `PathTarget` names the path by `TransferPath::id()` and carries the name for the file.

`CostModel` is built once from a `DeviceProfileSet` and never changes. `node_time()` is a node's
operations over the device's throughput for its kind, plus its read and written bytes over the
device's memory bandwidth. `transfer_time()` is the latency of a path plus the bytes over its
bandwidth; the path is looked up on the source device first, then on the destination.
`host_copy_time()` prices a copy over a device's host link in one direction, and `host_staged_time()`
prices a copy between two devices through host memory as the source's device-to-host link plus the
destination's host-to-device link. A device, a path or a link with no profile gives no time. A device
with a rate of zero that has work to do gives infinity.

`select_transfer_cost()` gives the planner its `TransferCost`: the model's seconds when a model is
loaded, `uniform_transfer_cost()` when none is. A copy the model cannot price costs infinity. The
runtime builds it once, at create, in `engine/execution`'s `load_transfer_cost()`, and hands it out as
`RuntimeContext::transfer_cost()`; the composer's and `PlanCache`'s hook prices every copy with it,
so the cheapest path and source win and eviction weighs the price of a write-back. The resolver maps
a device space to its device's `DeviceKey` and the host space, or a file space, to none. A copy between
two devices costs the path's own measurement, or the copy through host memory when the profile names no
such path, which prices `HostStagedPath`. A copy between a device and a space with no device costs
that device's host link, which prices host copies and `FilePath`. A copy no measurement covers costs
infinity, and the first such copy is logged as a warning.

## ProfileMeasurement

`ProfileMeasurement` fills a `DeviceProfile` through the `ProfileProbe` port. The probe copies bytes no
consumer reads and times a kernel the caller registered for each kind in the `MeasurementPlan`; the
measurement holds no kernel and no list of kinds. With `host_links` set, it also times each device's
copy from host memory and back, and stores them as the host links. The plan must name `OperationKinds::K_GENERIC`, whose
kernel gives the base throughput. Memory bandwidth counts the bytes read and written. A path's latency
is the time of the small copy, and its bandwidth is the large copy's bytes over its time less the
latency. A probe that reports a time of zero or less is refused as `InvalidState`.

## DeviceProfileProbe

`DeviceProfileProbe` is the probe on real devices. It uses standard SYCL only, so it builds under
AdaptiveCpp. `ProbeDevices` gives it each device's queue and memory space by `DeviceKey`. Capacity
and reserve are the space's. A device copy is one `memcpy` between two device allocations; a path
copy goes through the runtime's own `TransferPath` of that identity between an allocation on each
device, so it times what the planner will use; a kind runs the `ProbeKernel` the caller registered
for it, which returns the operations it did; a host copy is one `memcpy` between a device
allocation and a host allocation. Each runs once to warm up and once timed, on the host's
steady clock, from the submit to the end of the wait. The time is a measurement and decides no plan
until it is written to the profile file. A path identity no path connects is `Unsupported`; a
kind with no kernel, or a key with no device, is `InvalidArgument`.

Integrated detection is a hook, not a rule. When a backend reports `host_unified_memory` false for an
APU that carves its memory from RAM (HIP on gfx1036 is the open case of T42), the probe is where
the fact may be taken from measured behaviour instead, such as a device copy no faster than a host
copy. It is not done: no threshold is guessed until the closing run has measured one.

## Device profile file

`DeviceProfileFile` owns the file. It is read once at the start of a run and is frozen for that run.
The writer writes only the current version, devices in `DeviceKey` order, kinds by value and paths by
name, with doubles in 17 significant digits, so one set always gives the same bytes. The file is
written to a temporary file and renamed over the old one.

```
format_version 2

device cuda:uuid:00112233445566778899aabbccddeeff:0
capacity_bytes 12884901888
reserve_bytes 536870912
memory_bandwidth 9.1e+11
base_throughput 1.2e+13
throughput 3195455669 3.1e+13
path pcie 1.2e+10 1e-05
host_to_device 1.1e+10 8e-06
device_to_host 1.0e+10 9e-06
end
```

| Line | Holds |
| --- | --- |
| `format_version <integer>` | The first line. |
| `device <key>` | Opens a block; the key is the text of `to_text(DeviceKey)`. A key appears once. |
| `capacity_bytes`, `reserve_bytes` | Unsigned integers; each appears once per block. |
| `memory_bandwidth`, `base_throughput` | Finite numbers of zero or more; each appears once per block. |
| `throughput <kind> <value>` | `<kind>` is the `OperationKind` value as an unsigned integer. One per kind. |
| `path <name> <bandwidth> <latency>` | `<name>` is one word, the on-disk key. The reader maps it once to `TransferPathID::named(<name>)`, and the writer prints the name each entry keeps, in name order. One per name. |
| `host_to_device <bandwidth> <latency>`, `device_to_host <bandwidth> <latency>` | The host links; each appears at most once per block. A block without one leaves that link unmeasured. Version 2 only. |
| `end` | Closes the block. |

| Version | The reader |
| --- | --- |
| 1 | Reads it and upgrades it: it has no host links, so every link stays unmeasured. A host link line in it is a fault. |
| 2 | Reads it. It is the current version. |
| 3 or later | Rejects it with `Unsupported`, naming the version found and the versions known. |

A fault in a line is `InvalidArgument` and names the line. A file that is missing is refused by
`read()`. The runtime reads the file `RuntimeConfig::device_profile_file` names, once, at create. An
empty path runs with no `CostModel` and logs it. A path with no file is a first start: the runtime
measures every device and writes the file, which the next start reads. A file that is there and cannot
be read refuses the create with the reader's error.

## Folding the ledger into the profile

The profile entries a run measured go back into the profile file in three steps, one unit each, so a second
run reads them.

- `observe_dispatches()` turns the records `ProfilingLedger::collect()` appended to `RunObservation::events`
  into `DispatchObservation` values: the device that ran the dispatch, the cost its node declared in the
  `NodeTable`, and the seconds from the record's start to its end. A `DeviceKeyTable` maps a record's device
  index to a `DeviceKey` and may name the host's key. A record is dropped when its node has no cost or counts
  no operations, when its device has no key, or when its end does not follow its start. The unit reads the
  records and the table and names neither the ledger nor the scheduler.
- `fold_observations()` computes one throughput per observation, the node's operations over its seconds less
  the time its bytes take at the device's memory bandwidth, the inverse of how `CostModel::node_time()` prices
  a node, so the folded profile prices the same node at the measured time. The entry of a device and a kind
  moves toward the median of its samples, the lower one of an even count, and the generic kind moves
  `base_throughput`. A device and kind needs `K_MINIMUM_FOLD_SAMPLES` (8) samples in one fold before its entry
  moves; with fewer it keeps its value. It then moves `K_FOLD_SMOOTHING_WEIGHT` (0.25) of the way to the
  median, `new = old + 0.25 * (median - old)`, so one noisy horizon shifts a plan by a quarter of the distance
  at most and repeated horizons converge to the median. An entry with no positive value, a kind never
  measured or a zero base, takes the median outright. The median is robust against one slow dispatch and
  gives the same entry for any order of records. The arithmetic is plain double operations on the
  records, so the same inputs give the same doubles. A device with no profile receives nothing, since it is
  measured first; an observation whose time is at most what its bytes need, or whose time or operations are
  not positive and finite, is dropped.
- `update_profile_file()` reads the file the run was planned from, folds the observations into it and writes it
  through `DeviceProfileFile::write()`, which uses a temporary file. The file a run reads stays frozen for that
  run; the update is called when the run ends, and the next run reads the result. A missing file is an error
  and nothing is created.

The step driver of `surface/api` calls them through the `PartitionLedger` of the running graph's session:
after each run of a partitioned plan it adds the run's steps, its seconds and its dispatches, and at each
horizon of `coherence_horizon()` it folds the dispatches into the file `RuntimeConfig::device_profile_file`
names, under the lock of the runtime's `ProfileStore`, then calls that session's `PartitionSession::rebalance()`. A runtime with no profile file folds nothing. A failed write is logged
and the run goes on.
