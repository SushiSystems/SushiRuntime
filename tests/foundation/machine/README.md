# machine tests

The tests of the `machine` module, mirroring `modules/foundation/machine/`.
`tests/foundation/machine/CMakeLists.txt` builds them into `sr_machine_test_objects`, linked into `sr_foundation_tests`. `sr test` runs them
with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_device_filter.cpp

The file tests `admits_class()` and `admits()`, which decide on values which devices a
`DeviceFilter` accepts. That a default runtime drives a device of every class the machine has is
tested through `Runtime` in `tests/surface/api/runtime/test_runtime_config.cpp`
(`Medium_DefaultDeviceFilter`).

| Test | What it proves |
| --- | --- |
| `Small_DeviceFilterClass.TheDefaultFilterAcceptsTheClass` | Rows of the four classes: the default filter accepts each, with or without fp64. |
| `Small_DeviceFilterClass.AFilterThatAdmitsNoClassAcceptsOnlyAnUnlistedClass` | Rows of the four classes: a filter with every listed class off accepts only `DeviceKind::Other`. |
| `Small_DeviceFilter.TurningOneClassOffDropsThatClassAlone` | A filter with the GPU class off drops a GPU and keeps a CPU and an accelerator. |
| `Small_DeviceFilter.RequireFp64DropsADeviceWithoutIt` | With `require_fp64` set, a device without fp64 is dropped and one with it is kept. |

## test_node_facts.cpp

The file tests NodeFacts: a copy keeps the labels in order and every device.

| Test | What it proves |
| --- | --- |
| `ACopyKeepsTheLabelsInOrderAndTheDevices` | Checks that a copy keeps the label order and each device. |

## test_node_label.cpp

The file tests NodeLabel: a key and a value that copy as plain strings.

| Test | What it proves |
| --- | --- |
| `ACopyKeepsTheKeyAndTheValue` | Checks that a copy keeps both strings. |

## compile_device_description.cpp

Includes `device_description.hpp` alone, so the build fails if the header misses an include. It holds no test.

## compile_device_facts.cpp

Includes `device_facts.hpp` alone, so the build fails if the header misses an include. It holds no test.

## compile_device_kind.cpp

Includes `device_kind.hpp` alone, so the build fails if the header misses an include, and states at file scope that `DeviceKind` is stored in one byte. It holds no test.

## compile_operating_system.cpp

Includes `operating_system.hpp` alone, so the build fails if the header misses an include, and states at file scope that `OperatingSystem` is stored in one byte. It holds no test.

## compile_pin_policy.cpp

Includes `pin_policy.hpp` alone, so the build fails if the header misses an include, and states at file scope that `PinPolicy` is stored in one byte. It holds no test.

## compile_worker_pool.cpp

Includes `worker_pool.hpp` alone, so the build fails if the header misses an include. It holds no test.
