# sycl tests

The tests of the `sycl` module, mirroring `modules/foundation/sycl/`.
`tests/foundation/sycl/CMakeLists.txt` builds them into `sr_sycl_test_objects`, linked into `sr_foundation_tests`. `sr test` runs them with
the rest; `sr run sr_foundation_tests -- --gtest_filter='<pattern>'` runs one program alone.

A section is named after a test file, and a subsection after the test, helper or topic it describes.

## test_error_translation.cpp

The file tests the SYCL error translation: device codes, allocation failures and other throws.

| Test | What it proves |
| --- | --- |
| `ASYCLExceptionTakesTheDeviceCode` | A SYCL exception takes the device code with its text. |
| `AMemoryAllocationErrorIsAllocationFailed` | A SYCL memory allocation failure is AllocationFailed. |
| `AStandardExceptionTakesTheOtherCode` | A standard exception takes the other code. |
| `AReturningCallGivesAValue` | A call that returns gives a value. |

## test_sycl_compat.cpp

The file tests the host task trait and enqueue_host_task() on the program's queue, which the fixture base
of `tests/support/runtime_environment.hpp` hands out.

| Test | What it proves |
| --- | --- |
| `Small_SYCLCompat.TheTraitSeesAHostTaskMemberAndNoOther` | The trait is true for a handler type with a `host_task` member and false for one without. |
| `Medium_SYCLCompat.AHostTaskRunsOnce` | A host task enqueued through the shim runs once before wait() returns. |
