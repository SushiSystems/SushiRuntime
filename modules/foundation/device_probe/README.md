# device_probe

The module holds `has_any_sycl_device()`, the question a test asks before it needs a device. It is
an internal module built only with the tests: its headers are never installed and the library does
not link it.

It depends on SYCL and on no other module of the repository.

## Public entry points

| Header | Holds |
| --- | --- |
| `device_probe.hpp` | `has_any_sycl_device()` |

Every name is declared in `SushiRuntime::Test`.

## device_probe.hpp

`has_any_sycl_device` asks SYCL whether it reports a device and constructs nothing of the
runtime. A test that needs a device skips on its answer and builds its contexts outside any
catch, so a constructor that throws fails the test. An exception from SYCL itself also fails
it.
