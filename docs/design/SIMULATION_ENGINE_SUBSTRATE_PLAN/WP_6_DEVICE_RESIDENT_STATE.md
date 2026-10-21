# WP-6: Device-resident persistent state

**Problem:** `Buffer<T>`/`State<T>` allocate **shared** USM (`buffer.hpp:95-103`,
via `malloc_shared_on`). At 10⁶–10⁹ elements kept resident across frames (L2/L9),
shared USM risks host/device migration thrash, and the world must stay on the device
between frames with the host reading only selected sub-ranges on demand.

**Design:**
- A **device-USM** storage option for `State`/`Buffer` (the runtime already exposes
  `malloc_device`, `runtime_context.hpp:160`), selected at allocation behind the same
  handle API (L-principle: invisible to the user; `rt.state<T>(n)` unchanged; a
  policy/tag picks residency).
- **Selective readback**: a typed `read_range(offset, len)` that copies just the
  requested sub-region to a host staging buffer (uses WP-2 regions), for telemetry/IO
  (headless, L10) without host-mapping the whole field.
- Co-location stays correct via the existing owner map (`runtime_context.cpp:84`,
  `get_queue_for_ptr`); device residency does not change the dependency model.

**Files:** `buffer.hpp` (residency tag + `read_range`), `runtime_context.*`
(device-allocation path already present), interaction with WP-2 region views.

**Risks:** device USM is not host-addressable, so any host-side `operator[]`
(`buffer.hpp:154,275`) must be gated/removed for device-resident handles: a
compile-time or debug-time guard, since silently returning a device pointer to host
code is a fault. Multi-allocator magazine cliff (findings 3.6) is hit here under multi-GPU
(L4) and should be fixed alongside.

**Acceptance:** a 10⁸-element field stays device-resident across 10⁴ steps with no
host migration (verified by profiler), while `read_range` pulls a window correctly.
