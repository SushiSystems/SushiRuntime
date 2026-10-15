# settings.py
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.
"""Holds the settings of one repository that the shared checkers read.

copy_shared_tools.py writes this file only when a repository has none, and never overwrites it.
"""

from __future__ import annotations

K_TIER_ORDER: tuple[str, ...] = ("foundation", "platform", "engine", "surface", "extension")
K_DOMAIN_TIERS: tuple[str, ...] = ()
K_SAME_TIER_EDGES: frozenset[tuple[str, str]] = frozenset({
    ("report", "core"),
    ("log", "core"),
    ("logging", "core"),
    ("logging", "log"),
    ("sycl", "core"),
    ("test_runner", "logging"),
    ("topology", "numa_lookup"),
    ("dependency_tracking", "task"),
    ("work_stealing", "task"),
    ("scheduler", "task"),
    ("scheduler", "work_stealing"),
    ("graph", "dependency_tracking"),
    ("graph", "task"),
    ("execution", "task"),
    ("execution", "scheduler"),
    ("execution", "graph"),
    ("region_boundary", "task"),
    ("probe_tasks", "task"),
    ("runtime_assembly", "execution"),
    ("runtime_assembly", "scheduler"),
    ("memory_exchange", "interop"),
    ("context_access", "context"),
    ("api", "engine_handles"),
    ("api", "interop"),
    ("api", "memory_exchange"),
    ("api", "context"),
    ("api", "context_access"),
    ("wire", "transport"),
    ("network_socket", "transport"),
    ("frame_transport", "transport"),
    ("frame_transport", "wire"),
    ("buffer_exchange", "transport"),
    ("buffer_exchange", "wire"),
    ("pending_tasks", "transport"),
    ("offload_coordination", "transport"),
    ("offload_coordination", "wire"),
    ("offload_coordination", "link_security"),
    ("offload_coordination", "cluster_load"),
    ("offload_coordination", "buffer_exchange"),
    ("offload_coordination", "pending_tasks"),
    ("distributed", "cluster_load"),
    ("distributed", "offload_coordination"),
    ("distributed", "buffer_exchange"),
    ("distributed", "pending_tasks"),
    ("distributed", "dispatch_deduplication"),
    ("distributed", "process_environment"),
    ("distributed", "transport"),
    ("distributed", "wire"),
    ("distributed", "link_security"),
    ("distributed", "network_socket"),
    ("distributed", "frame_transport"),
})
K_MODULE_FUNCTION = "sushiruntime_add_module"
K_PUBLIC_DEPENDS = "DEPENDS"
K_PRIVATE_DEPENDS = "PRIVATE_DEPENDS"
K_INTERNAL_MARK = "INTERNAL"
K_CLOSED = False
K_MACRO_PREFIXES: tuple[str, ...] = ()
