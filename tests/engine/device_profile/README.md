# device_profile tests

The tests of the `device_profile` module, mirroring `modules/engine/device_profile/`.
`tests/engine/device_profile/CMakeLists.txt` builds them; `sr test` runs them with the rest.

## test_dispatch_observation.cpp

A record becomes the device, the declared cost and the seconds; a host record takes the host key or is
dropped; a record with an unknown node, no cost, no operations, an unknown device or no positive duration is
dropped; the order of the records is kept.

## test_profile_fold.cpp

A fold moves an entry the smoothing weight of the way to the median, the lower one of an even count, and only
with the minimum samples of a device and kind; below it nothing moves; one outlier horizon moves an entry by
at most the weight of the distance; repeated horizons converge to the median and price the node at the measured
time; an entry with no value takes the median; the generic kind moves the base throughput; the rest of every
profile is kept; an observation that says nothing changes nothing; the order of the observations does not
matter; the same inputs give the same doubles.

## test_profile_update.cpp

Through a profile file in the temporary directory: a second run reads the throughput the first one
measured as the fold gives it, a run below the minimum samples and a run that measured nothing write the same
profiles, a later run folds over an earlier one, and a missing file is an error that creates nothing.

## test_cost_model.cpp

A node's time is its operations over throughput plus its bytes over bandwidth, a kind with no entry
takes the base throughput, a device with no profile has no time, and a device that cannot do the
work takes infinite time. A copy's time is the latency plus the bytes over the bandwidth, and an
unknown path has no time. A path is priced by its identity and never by the name its entry carries.

## test_device_profile.cpp

A kind with no entry takes the base throughput, a path is found by its identity, and `set_path()` keys an entry by the identity of its name and keeps the name.

## test_device_profile_file.cpp

A profile written once and read back gives equal profiles and the same `CostModel` times, and a
changed file changes them. The write order does not depend on the insertion order, a device missing
from the file has no profile, a newer version is refused naming both versions, malformed text is
refused with its line, a path name with white space is refused on write, and a file on disk reads
back equal.

## test_device_profile_set.cpp

A device has a profile only after it is set, setting again replaces it, and profiles come in
`DeviceKey` order.

## test_profile_measurement.cpp

The profile holds the rates the fake probe reported, a faster device measures faster, only the kinds
of the plan are measured, and a plan with no generic kind, a large copy no bigger than the small one,
or a probe that reports no time is refused.

## test_profile_transfer_cost.cpp

With no model the cost is the uniform one, with a model it is the modelled seconds, and a copy the
model cannot price costs infinity. The price follows the path's identity, not the name the profile entry carries.

## test_device_profile_probe.cpp

Medium, on a SYCL CPU device; it skips when SYCL offers none. `ProfileMeasurement` measures a device
through `DeviceProfileProbe`: capacity and reserve are the space's, and memory bandwidth, base
throughput and the same-context path's bandwidth are above zero. A path name no path has is
`Unsupported`, a kind with no kernel and a key with no device are refused.

## compile_device_profile_log_category.cpp, compile_probe_devices.cpp and compile_profile_probe.cpp

Each file includes one header alone.
