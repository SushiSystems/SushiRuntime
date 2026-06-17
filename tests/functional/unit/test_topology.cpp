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

// Discovery must find at least one compute device, with sequential logical ids.
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

// ACPI SLIT distances: local node is exactly 10, a remote node is greater.
TEST(Unit_TopologyTest, NUMAMatrixDistances)
{
    NUMAMapper mapper;

    int local_distance = mapper.get_distance(0, 0);
    EXPECT_EQ(local_distance, 10) << "Local NUMA distance must be 10.";

    int remote_distance = mapper.get_distance(0, 1);
    EXPECT_GT(remote_distance, 10) << "Remote NUMA distance must be greater than 10.";
}

// Requesting a queue for a non-existent NUMA node must fall back, not crash.
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

// Requesting an allocator for an invalid NUMA node must fall back to node 0.
TEST(Unit_TopologyTest, InvalidNUMAFallbackAllocator)
{
    RuntimeContext ctx;

    constexpr int INVALID_NUMA_ID = -5; // negative edge case

    sushi_ptr<SushiRuntime::Memory::USMAllocator> allocator;

    EXPECT_NO_THROW({
        allocator = ctx.get_allocator(INVALID_NUMA_ID);
    }) << "Context crashed when creating an allocator for a negative NUMA ID.";

    ASSERT_NE(allocator.get(), nullptr) << "Fallback allocator is null.";

    void* ptr = allocator->allocate(128);
    EXPECT_NE(ptr, nullptr) << "Fallback allocator failed to assign memory.";

    allocator->deallocate(ptr);
}
