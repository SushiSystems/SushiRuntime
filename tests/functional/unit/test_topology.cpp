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
#include <optional>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;

namespace
{
    /** @brief Holds the number of USM kinds the aligned allocation tests cover. */
    constexpr std::size_t K_ALIGNED_KIND_COUNT = 3;

    /** @brief Lists the USM kinds the aligned allocation tests cover: shared, device, host. */
    const std::array<Memory::UsmKind, K_ALIGNED_KIND_COUNT> K_ALIGNED_KINDS = {
        Memory::UsmKind::Shared,
        Memory::UsmKind::Device,
        Memory::UsmKind::Host,
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
        sycl::queue& fallback_queue =
            ctx.devices().queue_or_numa_local(std::nullopt, INVALID_NUMA_ID);

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
        (void)ctx.devices().queue_or_numa_local(std::nullopt, INVALID_NUMA_ID);
    }) << "Context crashed when asked for a queue on a negative NUMA ID.";

    void* ptr = nullptr;
    EXPECT_NO_THROW({
        ptr = ctx.memory().allocate({Memory::UsmKind::Shared, 128, 0,
                                     ctx.devices().device_for_numa(INVALID_NUMA_ID)});
    }) << "Allocation on a negative NUMA ID threw instead of falling back.";

    ASSERT_NE(ptr, nullptr) << "Fallback allocation returned null.";
    EXPECT_TRUE(ctx.memory().device_of(ptr).has_value())
        << "The fallback allocation was not recorded against any device.";

    ctx.memory().release(ptr);
    EXPECT_FALSE(ctx.memory().device_of(ptr).has_value())
        << "Freeing left the pointer's owner recorded.";
}

/** @brief Tests that the live count follows each allocation, free and forgotten record. */
TEST(Unit_TopologyTest, LiveUsmCountFollowsAllocateFreeAndForget)
{
    RuntimeContext ctx;
    const std::size_t baseline = ctx.memory().live_count();

    void* shared = ctx.memory().allocate(
        {Memory::UsmKind::Shared, 128, 0, ctx.devices().device_for_numa(0)});
    void* device = ctx.memory().allocate(
        {Memory::UsmKind::Device, 128, 0, ctx.devices().device_for_numa(0)});
    ASSERT_NE(shared, nullptr);
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(ctx.memory().live_count(), baseline + 2);

    ctx.memory().release(shared);
    EXPECT_EQ(ctx.memory().live_count(), baseline + 1);

    ctx.memory().release(device);
    EXPECT_EQ(ctx.memory().live_count(), baseline);

    int external = 0;
    ctx.memory().adopt(&external, 0, sizeof(external));
    EXPECT_EQ(ctx.memory().live_count(), baseline + 1);
    ctx.memory().forget(&external);
    EXPECT_EQ(ctx.memory().live_count(), baseline);
}

/** @brief Tests that every aligned allocation returns an address on the requested alignment. */
TEST(Unit_TopologyTest, AlignedAllocationsStartOnTheAlignment)
{
    RuntimeContext ctx;

    for (const Memory::UsmKind kind : K_ALIGNED_KINDS)
    {
        for (const std::size_t alignment : {std::size_t{64}, std::size_t{128}})
        {
            for (const std::size_t bytes : {std::size_t{1}, std::size_t{100}, std::size_t{4096}})
            {
                void* ptr = ctx.memory().allocate({kind, bytes, alignment, 0});
                ASSERT_NE(ptr, nullptr) << bytes << " bytes at alignment " << alignment;
                EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0u)
                    << bytes << " bytes at alignment " << alignment;
                ctx.memory().release(ptr);
            }
        }
    }
}

/** @brief Tests that an aligned allocation is recorded once and dropped by release(). */
TEST(Unit_TopologyTest, AlignedAllocationsAreRecordedAndFreed)
{
    RuntimeContext ctx;
    const std::size_t baseline = ctx.memory().live_count();

    std::array<void*, K_ALIGNED_KIND_COUNT> pointers = {};
    for (std::size_t i = 0; i < K_ALIGNED_KINDS.size(); ++i)
    {
        pointers[i] = ctx.memory().allocate({K_ALIGNED_KINDS[i], 256, 128, 0});
        EXPECT_NE(pointers[i], nullptr);
        EXPECT_EQ(ctx.memory().live_count(), baseline + i + 1);
    }

    for (void* ptr : pointers)
        ctx.memory().release(ptr);
    EXPECT_EQ(ctx.memory().live_count(), baseline);
}

/** @brief Tests that an aligned allocation on a device index past the last one throws. */
TEST(Unit_TopologyTest, AnAlignedAllocationOnAMissingDeviceThrows)
{
    RuntimeContext ctx;
    const std::size_t missing_device = ctx.devices().count();

    EXPECT_THROW((void)ctx.memory().allocate({Memory::UsmKind::Shared, 64, 128, missing_device}),
                 std::runtime_error);
    EXPECT_THROW((void)ctx.memory().allocate({Memory::UsmKind::Device, 64, 128, missing_device}),
                 std::runtime_error);
}
