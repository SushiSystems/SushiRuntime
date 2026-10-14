/**************************************************************************/
/* test_device_queue_set.cpp                                              */
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
 * @file test_device_queue_set.cpp
 * @brief Tests DeviceQueueSet's inventory, queue table, NUMA routing and index checks.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>

using SushiRuntime::ErrorCode;
using SushiRuntime::Internal::Device::DeviceDescriptor;
using SushiRuntime::DeviceDescription;
using SushiRuntime::DeviceKind;
using SushiRuntime::Internal::Device::DeviceQueueSet;
using SushiRuntime::Internal::Device::QueueFlavour;

namespace
{
    /** @brief Returns the kind SYCL reports for a device. */
    DeviceKind kind_of(const sycl::device& device)
    {
        if (device.is_cpu())
            return DeviceKind::CPU;
        if (device.is_gpu())
            return DeviceKind::GPU;
        if (device.is_accelerator())
            return DeviceKind::Accelerator;
        return DeviceKind::Other;
    }

    /** @brief Builds the descriptor of one SYCL device on a chosen NUMA node. */
    DeviceDescriptor describe(const sycl::device& device, int numa_node)
    {
        DeviceDescription info;
        info.name = device.get_info<sycl::info::device::name>();
        info.kind = kind_of(device);
        info.backend = device.get_backend();
        info.numa_node = numa_node;
        info.memory_bytes = device.get_info<sycl::info::device::global_mem_size>();
        info.compute_units = device.get_info<sycl::info::device::max_compute_units>();
        return DeviceDescriptor{device, std::move(info)};
    }

    /** @brief Builds one descriptor per device SYCL reports, all on NUMA node 0. */
    std::vector<DeviceDescriptor> describe_all_devices()
    {
        std::vector<DeviceDescriptor> descriptors;
        for (const sycl::device& device : sycl::device::get_devices())
            descriptors.push_back(describe(device, 0));
        return descriptors;
    }

    /** @brief Builds descriptors of SYCL's first device, one per NUMA id given. */
    std::vector<DeviceDescriptor> describe_first_device_on(const std::vector<int>& numa_nodes)
    {
        const sycl::device device = sycl::device::get_devices().front();
        std::vector<DeviceDescriptor> descriptors;
        for (const int numa_node : numa_nodes)
            descriptors.push_back(describe(device, numa_node));
        return descriptors;
    }

} // namespace

TEST(Unit_DeviceQueueSet, CountAndInventoryFollowTheDescriptors)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const std::vector<DeviceDescriptor> descriptors = describe_all_devices();
    DeviceQueueSet set(descriptors, false);

    ASSERT_EQ(set.count(), descriptors.size());
    ASSERT_EQ(set.devices().size(), descriptors.size());
    for (std::size_t index = 0; index < descriptors.size(); ++index)
    {
        const DeviceDescription& info = set.devices()[index];
        EXPECT_EQ(info.index, index);
        EXPECT_EQ(info.name, descriptors[index].info.name);
        EXPECT_EQ(info.kind, descriptors[index].info.kind);
        EXPECT_EQ(info.backend, descriptors[index].info.backend);
        EXPECT_EQ(info.numa_node, 0);
        EXPECT_EQ(info.memory_bytes, descriptors[index].info.memory_bytes);
        EXPECT_EQ(info.compute_units, descriptors[index].info.compute_units);
        EXPECT_EQ(set.is_gpu(index), descriptors[index].device.is_gpu());
    }
}

TEST(Unit_DeviceQueueSet, TheSetNumbersDevicesByPosition)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    std::vector<DeviceDescriptor> descriptors = describe_first_device_on({0, 0});
    descriptors[0].info.index = 7;
    descriptors[1].info.index = 7;
    DeviceQueueSet set(std::move(descriptors), false);

    EXPECT_EQ(set.devices()[0].index, 0u);
    EXPECT_EQ(set.devices()[1].index, 1u);
}

TEST(Unit_DeviceQueueSet, PlainQueueIsOneObjectOnItsDevice)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const std::vector<DeviceDescriptor> descriptors = describe_all_devices();
    DeviceQueueSet set(descriptors, false);

    const std::vector<sycl::queue> copies = set.plain_queues();
    ASSERT_EQ(copies.size(), descriptors.size());
    for (std::size_t index = 0; index < descriptors.size(); ++index)
    {
        sycl::queue& first = set.queue(index);
        EXPECT_EQ(&first, &set.queue(index));
        EXPECT_EQ(first.get_device(), descriptors[index].device);
        EXPECT_TRUE(copies[index] == first);
        EXPECT_FALSE(first.has_property<sycl::property::queue::enable_profiling>());

        // A device that is neither a CPU nor a GPU gets an in-order plain queue.
        const bool needs_in_order =
            !descriptors[index].device.is_cpu() && !descriptors[index].device.is_gpu();
        EXPECT_EQ(first.is_in_order(), needs_in_order);
    }
}

TEST(Unit_DeviceQueueSet, EachFlavourIsBuiltOnceWithThePropertiesAsked)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_all_devices(), false);
    sycl::queue& plain = set.queue(0);

    const QueueFlavour flavours[] = {{false, false}, {false, true}, {true, false}, {true, true}};
    std::vector<sycl::queue*> built;
    for (const QueueFlavour flavour : flavours)
    {
        sycl::queue& queue = set.queue(0, flavour);
        EXPECT_EQ(&queue, &set.queue(0, flavour));
        EXPECT_EQ(queue.get_device(), plain.get_device());
        EXPECT_EQ(queue.get_context(), plain.get_context());
        EXPECT_EQ(queue.is_in_order(), flavour.in_order || plain.is_in_order());
        EXPECT_EQ(queue.has_property<sycl::property::queue::enable_profiling>(),
                  flavour.profiling);
        built.push_back(&queue);
    }

    // The flavour the plain queue already has is served by the plain queue itself.
    EXPECT_EQ(built[plain.is_in_order() ? 2 : 0], &plain);
    EXPECT_NE(built[1], built[0]);
    EXPECT_NE(built[3], built[2]);
    EXPECT_NE(built[3], &plain);
}

TEST(Unit_DeviceQueueSet, HasProfilingQueueTurnsTrueWhenOneIsBuilt)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({0, 0, 0}), false);

    EXPECT_FALSE(set.has_profiling_queue(0));
    (void)set.queue(0, QueueFlavour{true, false});
    EXPECT_FALSE(set.has_profiling_queue(0));

    (void)set.queue(0, QueueFlavour{false, true});
    EXPECT_TRUE(set.has_profiling_queue(0));

    (void)set.queue(1, QueueFlavour{true, true});
    EXPECT_TRUE(set.has_profiling_queue(1));

    EXPECT_FALSE(set.has_profiling_queue(2));
    EXPECT_FALSE(set.has_profiling_queue(set.count()));
}

TEST(Unit_DeviceQueueSet, AProfilingSetBuildsEveryPlainQueueWithProfiling)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_all_devices(), true);

    for (std::size_t index = 0; index < set.count(); ++index)
    {
        sycl::queue& plain = set.queue(index);
        EXPECT_TRUE(plain.has_property<sycl::property::queue::enable_profiling>());
        EXPECT_TRUE(set.has_profiling_queue(index));
        EXPECT_EQ(&set.queue(index, QueueFlavour{false, true}), &plain);
    }
}

TEST(Unit_DeviceQueueSet, DeviceForNUMAReturnsTheFirstDeviceOfTheNode)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({2, 0, 2}), false);

    EXPECT_EQ(set.device_for_numa(0), 1u);
    EXPECT_EQ(set.device_for_numa(2), 0u);
}

TEST(Unit_DeviceQueueSet, DeviceForNUMAFallsBackToDeviceZeroWhereNoDeviceSits)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({2, 0, 2}), false);

    EXPECT_EQ(set.device_for_numa(1), 0u);
    EXPECT_EQ(set.device_for_numa(3), 0u);
    EXPECT_EQ(set.device_for_numa(999), 0u);
    EXPECT_EQ(set.device_for_numa(-5), 0u);
}

TEST(Unit_DeviceQueueSet, AnInvalidNUMAIDInADescriptorBecomesNodeZero)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({1, -1, 4096}), false);

    EXPECT_EQ(set.devices()[0].numa_node, 1);
    EXPECT_EQ(set.devices()[1].numa_node, 0);
    EXPECT_EQ(set.devices()[2].numa_node, 0);
    EXPECT_EQ(set.device_for_numa(0), 1u);
}

TEST(Unit_DeviceQueueSet, QueueOrNUMALocalReturnsTheNamedDevice)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({0, 2, 2}), false);

    EXPECT_EQ(&set.queue_or_numa_local(2, 0), &set.queue(2));
    EXPECT_EQ(&set.queue_or_numa_local(0, 2), &set.queue(0));
}

TEST(Unit_DeviceQueueSet, QueueOrNUMALocalRotatesOverTheDevicesOfTheNode)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({0, 2, 2}), false);

    // The rotation cursors are per thread, so a fresh thread starts every rotation at zero.
    std::vector<sycl::queue*> on_node;
    std::vector<sycl::queue*> off_node;
    std::thread caller([&set, &on_node, &off_node]
    {
        for (int call = 0; call < 4; ++call)
            on_node.push_back(&set.queue_or_numa_local(std::nullopt, 2));
        for (int call = 0; call < 4; ++call)
            off_node.push_back(&set.queue_or_numa_local(std::nullopt, 1));
    });
    caller.join();

    const std::vector<sycl::queue*> expected_on_node = {
        &set.queue(1), &set.queue(2), &set.queue(1), &set.queue(2)};
    const std::vector<sycl::queue*> expected_off_node = {
        &set.queue(0), &set.queue(1), &set.queue(2), &set.queue(0)};
    EXPECT_EQ(on_node, expected_on_node);
    EXPECT_EQ(off_node, expected_off_node);
}

TEST(Unit_DeviceQueueSetDeathTest, ABadIndexStopsNamingIndexAndCount)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_first_device_on({0, 0}), false);
    static constexpr std::size_t BAD_INDEX = 17;
    const char* const pattern = "Device index 17 is out of bounds; the set holds 2 device";

    EXPECT_DEATH(static_cast<void>(set.queue(BAD_INDEX)), pattern);
    EXPECT_DEATH(static_cast<void>(set.queue(BAD_INDEX, QueueFlavour{true, false})), pattern);
    EXPECT_DEATH(static_cast<void>(set.is_gpu(BAD_INDEX)), pattern);
    EXPECT_DEATH(static_cast<void>(set.queue_or_numa_local(BAD_INDEX, 0)), pattern);
    EXPECT_DEATH(static_cast<void>(set.queue(set.count())), "Device index 2 is out of bounds");
}

TEST(Unit_DeviceQueueSetDeathTest, AnIndexIntoAnEmptySetStops)
{
    DeviceQueueSet set({}, false);

    EXPECT_DEATH(static_cast<void>(set.queue(0)), "the set holds 0 device");
}

TEST(Unit_DeviceQueueSet, AnEmptySetHoldsNothingAndResolvesNoNUMANode)
{
    DeviceQueueSet set({}, false);

    EXPECT_EQ(set.count(), 0u);
    EXPECT_TRUE(set.devices().empty());
    EXPECT_TRUE(set.plain_queues().empty());
    EXPECT_FALSE(set.has_profiling_queue(0));
    EXPECT_TRUE(set.wait_all().has_value());
    EXPECT_TRUE(set.raise_asynchronous_errors().has_value());
}

TEST(Unit_DeviceQueueSetDeathTest, ANUMALookupOnAnEmptySetStops)
{
    DeviceQueueSet set({}, false);
    const char* const pattern = "The device set holds no device";

    EXPECT_DEATH(static_cast<void>(set.device_for_numa(0)), pattern);
    EXPECT_DEATH(static_cast<void>(set.queue_or_numa_local(std::nullopt, 0)), pattern);
}

TEST(Unit_DeviceQueueSet, WaitAllReturnsAfterAKernelOnALazilyBuiltQueueFinished)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    DeviceQueueSet set(describe_all_devices(), false);
    sycl::queue& lazy = set.queue(0, QueueFlavour{true, true});
    ASSERT_NE(&lazy, &set.queue(0));

    constexpr std::int32_t WRITTEN = 42;
    std::int32_t* cell = sycl::malloc_shared<std::int32_t>(1, lazy);
    ASSERT_NE(cell, nullptr);
    *cell = 0;

    const sycl::event submitted = lazy.single_task([cell] { *cell = WRITTEN; });
    set.wait_all().value();

    EXPECT_EQ(submitted.get_info<sycl::info::event::command_execution_status>(),
              sycl::info::event_command_status::complete);
    EXPECT_EQ(*cell, WRITTEN);
    sycl::free(cell, lazy);
}
