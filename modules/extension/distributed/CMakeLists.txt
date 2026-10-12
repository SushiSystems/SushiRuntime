# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME distributed TIER extension
    SYCL
    SOURCES
        buffer_exchange.cpp
        buffer_registry.cpp
        cluster.cpp
        cluster_monitor.cpp
        dispatch_deduplicator.cpp
        dispatch_executor.cpp
        distributed_coordinator.cpp
        endpoint.cpp
        environment.cpp
        handshake.cpp
        hmac_sha256.cpp
        offload_policy.cpp
        in_process_transport.cpp
        kernel_registry.cpp
        master_link.cpp
        offload_dispatcher.cpp
        outbox.cpp
        peer_connection.cpp
        pending_tasks.cpp
        registered_task.cpp
        sha256.cpp
        shared_secret.cpp
        socket_posix.cpp
        socket_windows.cpp
        stop_signal.cpp
        task_resubmission.cpp
        tcp_transport.cpp
        wire_frame.cpp
        wire_messages.cpp
        worker_agent.cpp
        worker_buffer_cache.cpp
        worker_directory.cpp
        worker_main.cpp
        worker_options_parser.cpp
        worker_session.cpp
    DEPENDS core api
    PRIVATE_DEPENDS
        logging
        memory
        device
        task
        execution
        region
        log
        context_access
    EXTERNAL "$<$<PLATFORM_ID:Windows>:ws2_32>")

sushiruntime_add_module_tests(
    MODULE distributed
    SOURCES
        test_buffer_exchange.cpp
        test_buffer_registry.cpp
        test_distributed.cpp
        test_distributed_e2e.cpp
        test_distributed_jepsen.cpp
        test_distributed_tcp_faults.cpp
        test_handshake.cpp
        test_offload_dispatcher.cpp
        test_pending_tasks.cpp
        test_socket.cpp
        test_transport_conformance.cpp
        test_wire_format.cpp
        test_worker_bricks.cpp
        test_worker_directory.cpp
        test_worker_main.cpp)
