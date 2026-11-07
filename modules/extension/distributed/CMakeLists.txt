# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under the Apache License 2.0. See LICENSE.

sushiruntime_add_module(
    NAME distributed TIER extension
    SYCL
    SOURCES
        cluster.cpp
        distributed_coordinator.cpp
        peer_connection.cpp
        registered_task.cpp
        shared_secret.cpp
        tcp_transport.cpp
        worker_agent.cpp
        worker_main.cpp
        worker_options_parser.cpp
        worker_session.cpp
    DEPENDS core log machine api transport cluster_load worker_types
    PRIVATE_DEPENDS
        memory
        device
        task
        execution
        region
        context_access
        wire
        link_security
        network_socket
        frame_transport
        buffer_exchange
        pending_tasks
        dispatch_deduplication
        process_environment
        offload_coordination
        cluster_monitor
        master_link
        dispatch_executor
    EXTERNAL SushiLog::text)
