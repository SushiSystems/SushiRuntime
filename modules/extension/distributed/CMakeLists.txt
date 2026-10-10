# CMakeLists.txt
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

sushiruntime_add_module(
    NAME distributed TIER extension
    SYCL
    SOURCES
        buffer_registry.cpp
        cluster.cpp
        distributed_coordinator.cpp
        endpoint.cpp
        handshake.cpp
        hmac_sha256.cpp
        i_offload_policy.cpp
        in_process_transport.cpp
        kernel_registry.cpp
        sha256.cpp
        shared_secret.cpp
        socket_posix.cpp
        socket_windows.cpp
        tcp_transport.cpp
        wire_frame.cpp
        wire_messages.cpp
        worker_agent.cpp
    DEPENDS core memory task
    PRIVATE_DEPENDS
        logging
        device
        graph
        execution
        api
    EXTERNAL "$<$<PLATFORM_ID:Windows>:ws2_32>")

sushiruntime_add_module_tests(
    MODULE distributed
    SOURCES
        test_distributed.cpp
        test_distributed_e2e.cpp
        test_distributed_jepsen.cpp
        test_handshake.cpp
        test_socket.cpp
        test_wire_format.cpp)
