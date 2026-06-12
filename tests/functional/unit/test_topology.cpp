/**************************************************************************/
/* test_topology.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_topology.cpp
 * @brief Unit + edge-case coverage for hardware discovery and NUMA mapping.
 *
 * The invalid-NUMA fallback cases live here (not under benchmark) because they
 * are correctness/resilience checks: they assert the runtime degrades
 * gracefully on bad input rather than measuring any performance figure.
 */

#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

/**
 * @test HardwareDiscoveryProbe
 * @brief Ensures that the hardware discovery module correctly identifies
 * at least one compute device (usually the Host CPU) in the system.
 */
TEST(TopologyTest, HardwareDiscoveryProbe)
{
    HardwareDiscovery discovery;
    HardwareFilterConfig config;

    // We explicitly enable CPU to guarantee at least one device on CI pipelines
    config.include_cpu = true;
    config.include_gpu = true;
    config.include_accelerator = true;

    std::vector<HardwareNode> nodes = discovery.probe_all_devices(config);

    // The system must have at least one logical execution unit (Host CPU)
    ASSERT_FALSE(nodes.empty()) << "Hardware discovery failed to find any devices!";

    // Verify that the logical IDs are assigned sequentially
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        EXPECT_EQ(nodes[i].logical_id, i) << "Logical IDs are not sequential.";
        EXPECT_FALSE(nodes[i].name.empty()) << "Device name should not be empty.";
    }
}

/**
 * @test NUMAMatrixDistances
 * @brief Tests the hwloc NUMA mapper to ensure it correctly calculates
 * ACPI SLIT distances. Local nodes must cost 10, remote nodes > 10.
 */
TEST(TopologyTest, NUMAMatrixDistances)
{
    NUMAMapper mapper;

    // According to ACPI SLIT standards, local NUMA distance is always exactly 10.
    int local_distance = mapper.get_distance(0, 0);
    EXPECT_EQ(local_distance, 10) << "Local NUMA distance must be 10.";

    // Remote NUMA distance should strictly be greater than local distance (usually 21)
    int remote_distance = mapper.get_distance(0, 1);
    EXPECT_GT(remote_distance, 10) << "Remote NUMA distance must be greater than 10.";
}

/**
 * @test InvalidNUMAFallbackQueue
 * @brief Tests the resilience of the RuntimeContext. If a user requests a queue
 * for a non-existent NUMA node, it must gracefully fallback without crashing.
 */
TEST(TopologyTest, InvalidNUMAFallbackQueue)
{
    RuntimeContext ctx;

    // Request a queue for a highly improbable NUMA node ID
    constexpr int INVALID_NUMA_ID = 999;

    // Expect no exceptions to be thrown during the fallback process
    EXPECT_NO_THROW({
        sycl::queue& fallback_queue = ctx.get_numa_local_queue(INVALID_NUMA_ID);

        // Ensure the returned queue is actually valid by performing a dummy operation
        auto event = fallback_queue.submit([](sycl::handler& h)
        {
            h.host_task([](){ /* Dummy host work */ });
        });
        event.wait();
    }) << "Context crashed when an invalid NUMA ID was requested.";
}

/**
 * @test InvalidNUMAFallbackAllocator
 * @brief Tests the resilience of the Memory Pool. Requesting a USM Allocator
 * for an invalid NUMA node must safely fallback to the default (NUMA 0) allocator.
 */
TEST(TopologyTest, InvalidNUMAFallbackAllocator)
{
    RuntimeContext ctx;

    constexpr int INVALID_NUMA_ID = -5; // Negative NUMA ID edge case

    sushi_ptr<SushiRuntime::Memory::USMAllocator> allocator;

    EXPECT_NO_THROW({
        allocator = ctx.get_allocator(INVALID_NUMA_ID);
    }) << "Context crashed when creating an allocator for a negative NUMA ID.";

    ASSERT_NE(allocator.get(), nullptr) << "Fallback allocator is null.";

    // Test if the returned fallback allocator is fully functional
    void* ptr = allocator->allocate(128);
    EXPECT_NE(ptr, nullptr) << "Fallback allocator failed to assign memory.";

    allocator->deallocate(ptr);
}
