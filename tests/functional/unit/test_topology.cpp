/**************************************************************************/
/* test_topology.cpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_topology.cpp
 * @brief Tests hardware discovery, NUMA distances and the invalid-NUMA fallbacks.
 * @see tests/functional/unit/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

namespace
{
    /** @brief Names one aligned allocation entry point of RuntimeContext. */
    using AlignedAllocation = void* (RuntimeContext::*)(std::size_t, std::size_t, std::size_t);

    /** @brief Holds the number of aligned allocation entry points of RuntimeContext. */
    constexpr std::size_t K_ALIGNED_ALLOCATION_COUNT = 3;

    /** @brief Lists the aligned allocation entry points: shared, device, host. */
    const std::array<AlignedAllocation, K_ALIGNED_ALLOCATION_COUNT> K_ALIGNED_ALLOCATIONS = {
        &RuntimeContext::aligned_malloc_shared_on,
        &RuntimeContext::aligned_malloc_device_on,
        &RuntimeContext::aligned_malloc_host_on,
    };
} // namespace

/**
 * @brief Verifies that hardware discovery finds at least one compute device with sequential logical
 *        IDs.
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
 * @brief Verifies that requesting a queue for an invalid NUMA ID falls back safely instead of
 *        crashing.
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

/** @brief Tests that the live count follows each allocation, free and forgotten record. */
TEST(Unit_TopologyTest, LiveUsmCountFollowsAllocateFreeAndForget)
{
    RuntimeContext ctx;
    const std::size_t baseline = ctx.live_usm_count();

    void* shared = ctx.malloc_shared(128);
    void* device = ctx.malloc_device(128);
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(ctx.live_usm_count(), baseline + 2);

    ctx.free_usm(shared);
    EXPECT_EQ(ctx.live_usm_count(), baseline + 1);

    ctx.free_usm(device);
    EXPECT_EQ(ctx.live_usm_count(), baseline);

    int external = 0;
    ctx.adopt_usm(&external, 0, sizeof(external));
    EXPECT_EQ(ctx.live_usm_count(), baseline + 1);
    ctx.forget_usm(&external);
    EXPECT_EQ(ctx.live_usm_count(), baseline);
}

/** @brief Tests that every aligned allocation returns an address on the requested alignment. */
TEST(Unit_TopologyTest, AlignedAllocationsStartOnTheAlignment)
{
    RuntimeContext ctx;

    for (const AlignedAllocation allocate : K_ALIGNED_ALLOCATIONS)
    {
        for (const std::size_t alignment : {std::size_t{64}, std::size_t{128}})
        {
            for (const std::size_t bytes : {std::size_t{1}, std::size_t{100}, std::size_t{4096}})
            {
                void* ptr = (ctx.*allocate)(alignment, bytes, 0);
                ASSERT_NE(ptr, nullptr) << bytes << " bytes at alignment " << alignment;
                EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0u)
                    << bytes << " bytes at alignment " << alignment;
                ctx.free_usm(ptr);
            }
        }
    }
}

/** @brief Tests that an aligned allocation is recorded once and dropped by free_usm. */
TEST(Unit_TopologyTest, AlignedAllocationsAreRecordedAndFreed)
{
    RuntimeContext ctx;
    const std::size_t baseline = ctx.live_usm_count();

    std::array<void*, K_ALIGNED_ALLOCATION_COUNT> pointers = {};
    for (std::size_t i = 0; i < K_ALIGNED_ALLOCATIONS.size(); ++i)
    {
        pointers[i] = (ctx.*K_ALIGNED_ALLOCATIONS[i])(128, 256, 0);
        EXPECT_NE(pointers[i], nullptr);
        EXPECT_EQ(ctx.live_usm_count(), baseline + i + 1);
    }

    for (void* ptr : pointers)
        ctx.free_usm(ptr);
    EXPECT_EQ(ctx.live_usm_count(), baseline);
}

/** @brief Tests that an aligned allocation on a device index past the last one throws. */
TEST(Unit_TopologyTest, AnAlignedAllocationOnAMissingDeviceThrows)
{
    RuntimeContext ctx;
    const std::size_t missing_device = ctx.device_count();

    EXPECT_THROW((void)ctx.aligned_malloc_shared_on(128, 64, missing_device), std::runtime_error);
    EXPECT_THROW((void)ctx.aligned_malloc_device_on(128, 64, missing_device), std::runtime_error);
}
