# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME distributed TIER extension
    SYCL
    SOURCES
        cluster.cpp
        cluster_monitor.cpp
        dispatch_executor.cpp
        distributed_coordinator.cpp
        in_process_transport.cpp
        kernel_registry.cpp
        master_link.cpp
        peer_connection.cpp
        registered_task.cpp
        shared_secret.cpp
        task_resubmission.cpp
        tcp_transport.cpp
        worker_agent.cpp
        worker_buffer_cache.cpp
        worker_main.cpp
        worker_options_parser.cpp
        worker_session.cpp
    DEPENDS core api transport cluster_load
    PRIVATE_DEPENDS
        logging
        memory
        device
        task
        execution
        region
        log
        context_access
        wire
        link_security
        network_socket
        frame_transport
        buffer_exchange
        pending_tasks
        dispatch_deduplication
        process_environment
        offload_coordination)
