# SushiRuntimeMovedHeaders.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# Old include path, then the paths it forwards to, then a semicolon line. The build writes
# one forwarding header per row into an include root that only the library target carries,
# so no module can include an old path. The whole table goes in v0.5.0.
set(SUSHIRUNTIME_MOVED_HEADERS
    "SushiRuntime/core/dispatch_width.hpp|SushiRuntime/observability/dispatch_width.hpp"
    "SushiRuntime/core/event_timestamps.hpp|SushiRuntime/observability/event_timestamps.hpp"
    "SushiRuntime/core/logger.hpp|SushiRuntime/logging/logger.hpp"
    "SushiRuntime/core/profiling_ledger.hpp|SushiRuntime/observability/profiling_ledger.hpp"
    "SushiRuntime/core/resource_liveness.hpp|SushiRuntime/memory/resource_liveness.hpp"
    "SushiRuntime/core/resource_region.hpp|SushiRuntime/memory/resource_region.hpp"
    "SushiRuntime/core/run_report.hpp|SushiRuntime/observability/run_report.hpp"
    "SushiRuntime/core/sycl_compat.hpp|SushiRuntime/sycl/sycl_compat.hpp"
    "SushiRuntime/graph/late_bindings.hpp|SushiRuntime/task/late_bindings.hpp"
    "SushiRuntime/graph/node.hpp|SushiRuntime/task/node.hpp|SushiRuntime/task/node_widths.hpp"
    "SushiRuntime/graph/task_types.hpp|SushiRuntime/task/task_types.hpp"
    "SushiRuntime/interop/host_export.hpp|SushiRuntime/api/interop/host_export.hpp"
    "SushiRuntime/scheduler/ready_sink.hpp|SushiRuntime/task/ready_sink.hpp"
    "SushiRuntime/scheduler/task.hpp|SushiRuntime/task/task.hpp|SushiRuntime/scheduler/range_task.hpp"
    "SushiRuntime/version.hpp|SushiRuntime/core/version.hpp"
)
