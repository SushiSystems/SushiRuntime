# machine tests

The tests of the `machine` module, mirroring `modules/foundation/machine/`.
`tests/foundation/machine/CMakeLists.txt` builds them into `sr_machine_tests`. `sr test` runs them
with the rest; `sr run sr_machine_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_device_description.cpp

The file tests DeviceDescription: the values a description holds before discovery fills it.

| Test | What it proves |
| --- | --- |
| `DefaultsDescribeNoDevice` | Checks the defaults of a description. |

## test_device_facts.cpp

The file tests DeviceFacts: the values a record holds before a device fills it.

| Test | What it proves |
| --- | --- |
| `DefaultsDescribeNoDevice` | Checks the defaults of the record. |

## test_device_filter.cpp

The file tests DeviceFilter: every class accepted, double precision not required.

| Test | What it proves |
| --- | --- |
| `DefaultsAcceptEveryClass` | Checks the defaults of the filter. |

## test_device_kind.cpp

The file tests DeviceKind: four distinct classes in one byte.

| Test | What it proves |
| --- | --- |
| `FourDistinctClassesInOneByte` | Checks the size and the distinct values of the enumeration. |

## test_node_facts.cpp

The file tests NodeFacts: the values a record holds before a runtime fills it.

| Test | What it proves |
| --- | --- |
| `DefaultsDescribeNoMachine` | Checks the defaults of the record. |
| `ACopyKeepsTheLabelsInOrderAndTheDevices` | Checks that a copy keeps the label order and each device. |

## test_node_label.cpp

The file tests NodeLabel: a key and a value that copy as plain strings.

| Test | What it proves |
| --- | --- |
| `DefaultsHoldAnEmptyKeyAndValue` | Checks the defaults of a label. |
| `ACopyKeepsTheKeyAndTheValue` | Checks that a copy keeps both strings. |

## test_operating_system.cpp

The file tests OperatingSystem: three distinct systems in one byte.

| Test | What it proves |
| --- | --- |
| `ThreeDistinctSystemsInOneByte` | Checks the size and the distinct values of the enumeration. |

## test_pin_policy.cpp

The file tests PinPolicy: two distinct placements in one byte.

| Test | What it proves |
| --- | --- |
| `TwoPlacementsInOneByte` | Checks the size and the two values of the enumeration. |

## test_worker_pool.cpp

The file tests WorkerPool: an empty pool that pins by default.

| Test | What it proves |
| --- | --- |
| `DefaultsToAnEmptyPinnedPool` | Checks the defaults: no worker, core 0, pinned. |
