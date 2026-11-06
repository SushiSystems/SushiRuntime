# machine tests

The tests of the `machine` module, mirroring `modules/foundation/machine/`.
`tests/foundation/machine/CMakeLists.txt` builds them into `sr_machine_test_objects`, linked into `sr_foundation_tests`. `sr test` runs them
with the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## compile_device_filter.cpp

Includes `device_filter.hpp` alone, so the build fails if the header misses an include. The
promise that a default filter admits every device class needs a `Runtime`, which sits above this
tier; it moves to the `surface/api` runtime tests in job M7.

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
