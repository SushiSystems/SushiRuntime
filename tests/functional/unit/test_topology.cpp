/**************************************************************************/
/* test_topology.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Unit + edge-case coverage for hardware discovery and NUMA mapping, including
// the invalid-NUMA fallbacks — correctness checks that the runtime degrades
// gracefully on bad input.

#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

/**
 * @brief Verifies that hardware discovery finds at least one compute device with sequential logical IDs.
 *
 * Probes all devices and ensures the returned list is not empty, possesses sequential IDs, and has valid names.
 */
TEST(Unit_TopologyTest, HardwareDiscoveryProbe)
{
    HardwareDiscovery discovery;
    HardwareFilterConfig config;
    config.include_cpu         = true;
    config.include_gpu         = true;
    config.include_accelerator = true;

    std::vector<HardwareNode> nodes = discovery.probe_all_devices(config);

    ASSERT_FALSE(nodes.empty()) << "Hardware discovery failed to find any devices!";

    for (size_t i = 0; i < nodes.size(); ++i)
    {
        EXPECT_EQ(nodes[i].logical_id, i) << "Logical IDs are not sequential.";
        EXPECT_FALSE(nodes[i].name.empty()) << "Device name should not be empty.";
    }
}

/**
 * @brief Verifies basic NUMA distance matrix properties as per ACPI SLIT standards.
 *
 * Asserts that the local node distance is exactly 10, and a remote node distance is strictly greater than 10.
 */
TEST(Unit_TopologyTest, NUMAMatrixDistances)
{
    NUMAMapper mapper;

    int local_distance = mapper.get_distance(0, 0);
    EXPECT_EQ(local_distance, 10) << "Local NUMA distance must be 10.";

    int remote_distance = mapper.get_distance(0, 1);
    EXPECT_GT(remote_distance, 10) << "Remote NUMA distance must be greater than 10.";
}

/**
 * @brief Verifies that requesting a queue for an invalid NUMA ID falls back safely instead of crashing.
 *
 * Requests a queue with a non-existent NUMA ID and validates that the returned fallback queue functions normally.
 */
TEST(Unit_TopologyTest, InvalidNUMAFallbackQueue)
{
    RuntimeContext ctx;

    constexpr int INVALID_NUMA_ID = 999;

    EXPECT_NO_THROW({
        sycl::queue& fallback_queue = ctx.get_numa_local_queue(INVALID_NUMA_ID);

        // Confirm the returned queue is usable with a dummy host task.
        auto event = fallback_queue.submit([](sycl::handler& h)
        {
            SushiRuntime::detail::enqueue_host_task(h, []() {});
        });
        event.wait();
    }) << "Context crashed when an invalid NUMA ID was requested.";
}

/**
 * @brief Verifies that an out-of-range NUMA ID falls back to a usable node.
 *
 * A negative id is what a caller gets from a topology that reported no NUMA
 * information, so it has to resolve to something rather than index out of
 * bounds. Allocating and freeing through it proves the fallback queue is a real
 * one and that the pointer's owner was recorded against it.
 */
TEST(Unit_TopologyTest, InvalidNUMAFallsBackToAUsableNode)
{
    RuntimeContext ctx;

    constexpr int INVALID_NUMA_ID = -5; // negative edge case

    EXPECT_NO_THROW({
        (void)ctx.get_numa_local_queue(INVALID_NUMA_ID);
    }) << "Context crashed when asked for a queue on a negative NUMA ID.";

    void* ptr = nullptr;
    EXPECT_NO_THROW({
        ptr = ctx.malloc_shared(128, INVALID_NUMA_ID);
    }) << "Allocation on a negative NUMA ID threw instead of falling back.";

    ASSERT_NE(ptr, nullptr) << "Fallback allocation returned null.";
    EXPECT_GE(ctx.context_index_for_ptr(ptr), 0)
        << "The fallback allocation was not recorded against any device.";

    ctx.free_usm(ptr);
    EXPECT_EQ(ctx.context_index_for_ptr(ptr), -1)
        << "Freeing left the pointer's owner recorded.";
}
