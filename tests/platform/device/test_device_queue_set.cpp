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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_device_queue_set.cpp
 * @brief Tests DeviceQueueSet's inventory, queue table, NUMA routing and index checks.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/in_memory_queue_factory.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/log/sushiruntime_log_project.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

#include "support/runtime_environment.hpp"

using SushiRuntime::Internal::Device::DeviceDescriptor;
using SushiRuntime::DeviceDescription;
using SushiRuntime::DeviceKind;
using SushiRuntime::Internal::Device::DeviceQueueSet;
using SushiRuntime::Internal::Device::InMemoryQueueFactory;
using SushiRuntime::Internal::Device::QueueBuild;
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

    /** @brief Describes the first plain queue of @p set that is not one object on its device, or returns an empty text. */
    std::string plain_queue_defect(DeviceQueueSet& set, const std::vector<DeviceDescriptor>& descriptors)
    {
        const std::vector<sycl::queue> copies = set.plain_queues();
        if (copies.size() != descriptors.size())
            return "plain_queues() holds " + std::to_string(copies.size()) + " queues";
        for (std::size_t index = 0; index < descriptors.size(); ++index)
        {
            sycl::queue& first = set.queue(index);
            if (&first != &set.queue(index))
                return "queue(" + std::to_string(index) + ") returned two objects";
            if (first.get_device() != descriptors[index].device)
                return "queue(" + std::to_string(index) + ") sits on another device";
            if (!(copies[index] == first))
                return "the copy of queue " + std::to_string(index) + " is another queue";
        }
        return {};
    }

    /** @brief Describes the first plain queue of @p set whose profiling property differs from @p profiling, or returns an empty text. */
    std::string profiling_defect(DeviceQueueSet& set, bool profiling)
    {
        for (std::size_t index = 0; index < set.count(); ++index)
        {
            sycl::queue& plain = set.queue(index);
            if (plain.has_property<sycl::property::queue::enable_profiling>() != profiling)
                return "the plain queue of device " + std::to_string(index) + " disagrees on profiling";
            if (set.has_profiling_queue(index) != profiling)
                return "has_profiling_queue(" + std::to_string(index) + ") disagrees";
            if (profiling && &set.queue(index, QueueFlavour{false, true}) != &plain)
                return "the profiling flavour of device " + std::to_string(index) + " is not the plain queue";
        }
        return {};
    }

    /** @brief Fixture base of the tests that read a real device; they skip where SYCL reports none. */
    class DeviceFixture : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Skips the test when SYCL reports no device. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";
            }
    };

    /** @brief One flavour, and whether the plain queue serves it when the plain queue is in order or out of order. */
    struct FlavourRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief The flavour asked for. */
        QueueFlavour flavour;
        /** @brief Whether the plain queue itself serves the flavour when the plain queue is in order. */
        bool plain_when_in_order;
        /** @brief Whether the plain queue itself serves the flavour when the plain queue is out of order. */
        bool plain_when_out_of_order;
    };

    /** @brief One host inventory the set must list as given, and the stale index each entry carries. */
    struct InventoryRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief The entries given to the set; the set renumbers their index. */
        std::vector<DeviceDescription> given;
    };

    /** @brief Builds one description of a host device for the inventory rows. */
    DeviceDescription inventory_entry(const char* name, DeviceKind kind, std::size_t stale_index)
    {
        DeviceDescription info;
        info.index = stale_index;
        info.name = name;
        info.kind = kind;
        info.backend = sycl::backend::opencl;
        info.numa_node = 0;
        info.memory_bytes = std::uint64_t{8} << 30;
        info.compute_units = 12;
        return info;
    }

    /** @brief Returns the inventories the set is built from: mixed kinds, and a shared stale index. */
    std::vector<InventoryRow> inventory_rows()
    {
        return {InventoryRow{"CPUAndGPU",
                             {inventory_entry("host cpu", DeviceKind::CPU, 0),
                              inventory_entry("host gpu", DeviceKind::GPU, 1)}},
                InventoryRow{"TwoDescriptorsWithTheSameStaleIndex",
                             {inventory_entry("host a", DeviceKind::CPU, 7),
                              inventory_entry("host b", DeviceKind::GPU, 7)}}};
    }

    /** @brief One call on a set, and the message a stop must carry. */
    struct StopRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief How many descriptors of SYCL's first device the set holds; zero builds an empty set. */
        std::size_t device_count;
        /** @brief The call that must stop the process. */
        std::function<void(DeviceQueueSet&)> call;
        /** @brief The regular expression the stop message matches. */
        const char* pattern;
    };

    /** @brief One NUMA node the rotation runs over, and the devices it must hand out in order. */
    struct RotationRow
    {
        /** @brief The row name gtest prints. */
        const char* name;
        /** @brief The node the rotation runs over. */
        int numa_node;
        /** @brief The device index of each of four calls. */
        std::vector<std::size_t> devices;
    };

    /** @brief Returns the queues four calls of a fresh thread get for @p numa_node; the thread starts every rotation at zero. */
    std::vector<sycl::queue*> rotate_on_fresh_thread(DeviceQueueSet& set, int numa_node)
    {
        static constexpr int K_CALLS = 4;
        std::vector<sycl::queue*> handed;
        std::thread caller([&set, &handed, numa_node]
        {
            for (int call = 0; call < K_CALLS; ++call)
                handed.push_back(&set.queue_or_numa_local(std::nullopt, numa_node));
        });
        caller.join();
        return handed;
    }

} // namespace

class Medium_DeviceQueueSet : public DeviceFixture
{
};

/** @brief Tests that the plain queue of every device is one object on that device. */
TEST_F(Medium_DeviceQueueSet, PlainQueueIsOneObjectOnItsDevice)
{
    const std::vector<DeviceDescriptor> descriptors = describe_all_devices();
    DeviceQueueSet set(descriptors, false);

    EXPECT_EQ(plain_queue_defect(set, descriptors), "");
}

class Medium_DeviceQueueSetFlavour : public DeviceFixture, public ::testing::WithParamInterface<FlavourRow>
{
};

/** @brief Tests that a flavour is built once, on the plain queue's device and context, with the properties asked. */
TEST_P(Medium_DeviceQueueSetFlavour, IsBuiltOnceWithThePropertiesAsked)
{
    const FlavourRow& row = GetParam();
    DeviceQueueSet set(describe_all_devices(), false);
    sycl::queue& plain = set.queue(0);

    sycl::queue& queue = set.queue(0, row.flavour);

    EXPECT_EQ(&queue, &set.queue(0, row.flavour));
    EXPECT_EQ(queue.get_device(), plain.get_device());
    EXPECT_EQ(queue.get_context(), plain.get_context());
    EXPECT_EQ(queue.is_in_order(), row.flavour.in_order || plain.is_in_order());
    EXPECT_EQ(queue.has_property<sycl::property::queue::enable_profiling>(), row.flavour.profiling);
    const bool plain_serves = plain.is_in_order() ? row.plain_when_in_order : row.plain_when_out_of_order;
    EXPECT_EQ(&queue == &plain, plain_serves);
}

INSTANTIATE_TEST_SUITE_P(Flavours, Medium_DeviceQueueSetFlavour,
                         ::testing::Values(FlavourRow{"OutOfOrderPlain", QueueFlavour{false, false}, true, true},
                                           FlavourRow{"InOrderPlain", QueueFlavour{true, false}, true, false},
                                           FlavourRow{"OutOfOrderProfiling", QueueFlavour{false, true}, false, false},
                                           FlavourRow{"InOrderProfiling", QueueFlavour{true, true}, false, false}),
                         [](const ::testing::TestParamInfo<FlavourRow>& info) { return std::string(info.param.name); });

class Medium_DeviceQueueSetProfiling : public DeviceFixture, public ::testing::WithParamInterface<bool>
{
};

/** @brief Tests that the profiling flag of the set decides the property of every plain queue. */
TEST_P(Medium_DeviceQueueSetProfiling, TheFlagOfTheSetDecidesEveryPlainQueue)
{
    DeviceQueueSet set(describe_all_devices(), GetParam());

    EXPECT_EQ(profiling_defect(set, GetParam()), "");
}

INSTANTIATE_TEST_SUITE_P(Flags, Medium_DeviceQueueSetProfiling, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? "WithProfiling" : "WithoutProfiling"; });

class Medium_DeviceQueueSetRotation : public DeviceFixture, public ::testing::WithParamInterface<RotationRow>
{
};

/** @brief Tests that the rotation over a node's devices hands each one out in turn on a fresh thread. */
TEST_P(Medium_DeviceQueueSetRotation, HandsOutTheDevicesOfTheNodeInTurn)
{
    const RotationRow& row = GetParam();
    DeviceQueueSet set(describe_first_device_on({0, 2, 2}), false);

    const std::vector<sycl::queue*> handed = rotate_on_fresh_thread(set, row.numa_node);

    std::vector<sycl::queue*> expected;
    for (const std::size_t device : row.devices)
        expected.push_back(&set.queue(device));
    EXPECT_EQ(handed, expected);
}

INSTANTIATE_TEST_SUITE_P(Nodes, Medium_DeviceQueueSetRotation,
                         ::testing::Values(RotationRow{"ANodeWithTwoDevices", 2, {1, 2, 1, 2}},
                                           RotationRow{"ANodeWithNoDeviceRotatesOverAll", 1, {0, 1, 2, 0}}),
                         [](const ::testing::TestParamInfo<RotationRow>& info) { return std::string(info.param.name); });

class Medium_DeviceQueueSetDeathTest : public TestSupport::RuntimeTest, public ::testing::WithParamInterface<StopRow>
{
};

/** @brief Tests that a lookup that names no device stops the process, naming the index and the count or the empty set. */
TEST_P(Medium_DeviceQueueSetDeathTest, ABadLookupStopsNamingItsCause)
{
    const StopRow& row = GetParam();
    if (row.device_count > 0 && !SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";
    std::vector<DeviceDescriptor> descriptors;
    if (row.device_count > 0)
        descriptors = describe_first_device_on(std::vector<int>(row.device_count, 0));
    DeviceQueueSet set(std::move(descriptors), false);

    EXPECT_DEATH(row.call(set), row.pattern);
}

INSTANTIATE_TEST_SUITE_P(
    Lookups, Medium_DeviceQueueSetDeathTest,
    ::testing::Values(
        StopRow{"PlainQueueOfABadIndex", 2, [](DeviceQueueSet& set) { static_cast<void>(set.queue(17)); },
                "Device index 17 is out of bounds; the set holds 2 device"},
        StopRow{"FlavouredQueueOfABadIndex", 2,
                [](DeviceQueueSet& set) { static_cast<void>(set.queue(17, QueueFlavour{true, false})); },
                "Device index 17 is out of bounds; the set holds 2 device"},
        StopRow{"IsGpuOfABadIndex", 2, [](DeviceQueueSet& set) { static_cast<void>(set.is_gpu(17)); },
                "Device index 17 is out of bounds; the set holds 2 device"},
        StopRow{"NumaLocalQueueOfABadIndex", 2,
                [](DeviceQueueSet& set) { static_cast<void>(set.queue_or_numa_local(17, 0)); },
                "Device index 17 is out of bounds; the set holds 2 device"},
        StopRow{"TheIndexEqualToTheCount", 2, [](DeviceQueueSet& set) { static_cast<void>(set.queue(set.count())); },
                "Device index 2 is out of bounds"},
        StopRow{"AnIndexIntoAnEmptySet", 0, [](DeviceQueueSet& set) { static_cast<void>(set.queue(0)); },
                "the set holds 0 device"},
        StopRow{"ANumaLookupOnAnEmptySet", 0, [](DeviceQueueSet& set) { static_cast<void>(set.device_for_numa(0)); },
                "The device set holds no device"},
        StopRow{"ARotationOnAnEmptySet", 0,
                [](DeviceQueueSet& set) { static_cast<void>(set.queue_or_numa_local(std::nullopt, 0)); },
                "The device set holds no device"}),
    [](const ::testing::TestParamInfo<StopRow>& info) { return std::string(info.param.name); });

/** @brief Tests that an empty set holds no device, no inventory entry and no queue. */
TEST(Small_DeviceQueueSet, AnEmptySetHoldsNothing)
{
    DeviceQueueSet set({}, false);

    EXPECT_EQ(set.count(), 0u);
    EXPECT_TRUE(set.devices().empty());
    EXPECT_TRUE(set.plain_queues().empty());
    EXPECT_FALSE(set.has_profiling_queue(0));
}

/** @brief Tests that waiting on an empty set succeeds. */
TEST(Small_DeviceQueueSet, WaitingOnAnEmptySetSucceeds)
{
    DeviceQueueSet set({}, false);

    EXPECT_TRUE(set.wait_all().has_value());
}

/** @brief Tests that raising the asynchronous errors of an empty set succeeds. */
TEST(Small_DeviceQueueSet, RaisingTheAsynchronousErrorsOfAnEmptySetSucceeds)
{
    DeviceQueueSet set({}, false);

    EXPECT_TRUE(set.raise_asynchronous_errors().has_value());
}

/** @brief Tests that wait_all waits for a kernel on a queue that was built lazily. */
TEST_F(Medium_DeviceQueueSet, WaitAllReturnsAfterAKernelOnALazilyBuiltQueueFinished)
{
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

namespace
{
    /** @brief Returns the inventory of a host set: two CPU devices, the second on a NUMA node out of range. */
    std::vector<DeviceDescription> host_inventory()
    {
        DeviceDescription first;
        first.name = "host a";
        first.kind = DeviceKind::CPU;
        DeviceDescription second;
        second.name = "host b";
        second.kind = DeviceKind::CPU;
        second.numa_node = -3;
        return {first, second};
    }
} // namespace

/** @brief Tests that a host set lists its descriptions in order, each with its index. */
TEST(Small_HostDeviceQueueSet, ListsTheDescriptionsInOrderWithTheirIndex)
{
    DeviceQueueSet set(host_inventory());

    ASSERT_EQ(set.count(), 2u);
    EXPECT_EQ(set.devices()[1].name, "host b");
    EXPECT_EQ(set.devices()[1].index, 1u);
}

/** @brief Tests that a host set places a NUMA node out of range on node zero. */
TEST(Small_HostDeviceQueueSet, PlacesANUMANodeOutOfRangeOnNodeZero)
{
    DeviceQueueSet set(host_inventory());

    EXPECT_EQ(set.devices()[1].numa_node, 0);
}

/** @brief Tests that a host set hands out no plain queue. */
TEST(Small_HostDeviceQueueSet, HoldsNoPlainQueue)
{
    DeviceQueueSet set(host_inventory());

    EXPECT_TRUE(set.plain_queues().empty());
}

/** @brief Tests that waiting on a host set succeeds, since it holds no queue to wait on. */
TEST(Small_HostDeviceQueueSet, WaitingOnAllSucceeds)
{
    DeviceQueueSet set(host_inventory());

    EXPECT_TRUE(set.wait_all().has_value());
}

/** @brief Tests that asking a host set for a queue stops the process, naming the device. */
TEST(Medium_HostDeviceQueueSetDeathTest, AskingForAQueueStopsAndNamesTheDevice)
{
    DeviceQueueSet set(host_inventory());

    EXPECT_DEATH(static_cast<void>(set.queue(0)), "host a");
}

/** @brief Runs the inventory test over the rows of inventory_rows(). */
class Small_HostDeviceQueueSetInventory : public ::testing::TestWithParam<InventoryRow>
{
};

/** @brief Tests that a host set lists every description as given, renumbered and on node zero. */
TEST_P(Small_HostDeviceQueueSetInventory, CountAndInventoryFollowTheDescriptions)
{
    const std::vector<DeviceDescription>& given = GetParam().given;
    DeviceQueueSet set(given);

    ASSERT_EQ(set.count(), given.size());
    ASSERT_EQ(set.devices().size(), given.size());
    for (std::size_t index = 0; index < given.size(); ++index)
    {
        const DeviceDescription& info = set.devices()[index];
        EXPECT_EQ(info.index, index);
        EXPECT_EQ(info.name, given[index].name);
        EXPECT_EQ(info.kind, given[index].kind);
        EXPECT_EQ(info.backend, given[index].backend);
        EXPECT_EQ(info.numa_node, 0);
        EXPECT_EQ(info.memory_bytes, given[index].memory_bytes);
        EXPECT_EQ(info.compute_units, given[index].compute_units);
        EXPECT_EQ(set.is_gpu(index), given[index].kind == DeviceKind::GPU);
    }
}

INSTANTIATE_TEST_SUITE_P(Inventories, Small_HostDeviceQueueSetInventory, ::testing::ValuesIn(inventory_rows()),
                         [](const ::testing::TestParamInfo<InventoryRow>& info) { return std::string(info.param.name); });

/** @brief Tests that a host set places a NUMA id that names no node on node zero and routes node zero to the device on node one. */
TEST(Small_HostDeviceQueueSet, AnInvalidNUMAIDInADescriptionBecomesNodeZero)
{
    std::vector<DeviceDescription> given = {inventory_entry("host a", DeviceKind::CPU, 0),
                                            inventory_entry("host b", DeviceKind::CPU, 1),
                                            inventory_entry("host c", DeviceKind::CPU, 2)};
    given[0].numa_node = 1;
    given[1].numa_node = -1;
    given[2].numa_node = 4096;
    DeviceQueueSet set(given);

    EXPECT_EQ(set.devices()[0].numa_node, 1);
    EXPECT_EQ(set.devices()[1].numa_node, 0);
    EXPECT_EQ(set.devices()[2].numa_node, 0);
    EXPECT_EQ(set.device_for_numa(0), 1u);
}

namespace
{
    /** @brief A set over an InMemoryQueueFactory, and the factory it builds through. */
    struct InMemorySet
    {
        /** @brief Builds a set of one CPU device per entry of @p numa_nodes. */
        explicit InMemorySet(const std::vector<int>& numa_nodes, std::vector<bool> in_order = {},
                             bool profiling = false)
            : factory(new InMemoryQueueFactory(numa_nodes.size(), std::move(in_order))),
              set(descriptions(numa_nodes), std::unique_ptr<InMemoryQueueFactory>(factory), profiling)
        {
        }

        /** @brief Returns one CPU description per entry of @p numa_nodes. */
        static std::vector<DeviceDescription> descriptions(const std::vector<int>& numa_nodes)
        {
            std::vector<DeviceDescription> given;
            for (std::size_t index = 0; index < numa_nodes.size(); ++index)
            {
                given.push_back(inventory_entry("in-memory", DeviceKind::CPU, index));
                given.back().numa_node = numa_nodes[index];
            }
            return given;
        }

        /// @brief The factory, owned by set.
        InMemoryQueueFactory* factory;
        /// @brief The set under test.
        DeviceQueueSet set;
    };
} // namespace

/** @brief Tests that the set builds each plain queue once, at construction, with the set's profiling flag. */
TEST(Small_DeviceQueueSet, BuildsEveryPlainQueueOnceAtConstruction)
{
    InMemorySet built({0, 0}, {}, true);

    const std::vector<QueueBuild> builds = built.factory->builds();
    ASSERT_EQ(builds.size(), 2u);
    EXPECT_EQ(builds[1].device, 1u);
    EXPECT_TRUE(builds[1].flavour.profiling);
    EXPECT_FALSE(builds[1].shared);
}

/** @brief Tests that the plain queues of two devices differ. */
TEST(Small_DeviceQueueSet, ThePlainQueuesOfTwoDevicesDiffer)
{
    InMemorySet built({0, 0});

    EXPECT_FALSE(built.set.queue(0) == built.set.queue(1));
}

/** @brief Tests that a lazy flavour is built once, in the context of its device's plain queue. */
TEST(Small_DeviceQueueSet, ALazyFlavourIsBuiltOnceInThePlainQueuesContext)
{
    InMemorySet built({0});

    sycl::queue& first = built.set.queue(0, QueueFlavour{true, true});
    sycl::queue& second = built.set.queue(0, QueueFlavour{true, true});

    EXPECT_EQ(&first, &second);
    const std::vector<QueueBuild> builds = built.factory->builds();
    ASSERT_EQ(builds.size(), 2u);
    EXPECT_TRUE(builds[1].shared);
    EXPECT_TRUE(first.get_context() == built.set.queue(0).get_context());
}

/** @brief Tests that a device whose plain queue is in order is never served an out-of-order queue. */
TEST(Small_DeviceQueueSet, ADeviceThatNeedsInOrderIsServedInOrder)
{
    InMemorySet built({0}, {true});

    (void)built.set.queue(0, QueueFlavour{false, true});

    const std::vector<QueueBuild> builds = built.factory->builds();
    ASSERT_EQ(builds.size(), 2u);
    EXPECT_TRUE(builds[0].flavour.in_order);
    EXPECT_TRUE(builds[1].flavour.in_order);
}

/** @brief Tests that an in-order queue without profiling leaves has_profiling_queue false. */
TEST(Small_DeviceQueueSet, AnInOrderBuildWithoutProfilingLeavesTheProfilingFlagFalse)
{
    InMemorySet built({0});

    EXPECT_FALSE(built.set.has_profiling_queue(0));
    (void)built.set.queue(0, QueueFlavour{true, false});

    EXPECT_FALSE(built.set.has_profiling_queue(0));
}

/** @brief Tests that building a profiling queue turns has_profiling_queue true. */
TEST(Small_DeviceQueueSet, AProfilingBuildTurnsTheProfilingFlagTrue)
{
    InMemorySet built({0});

    (void)built.set.queue(0, QueueFlavour{false, true});

    EXPECT_TRUE(built.set.has_profiling_queue(0));
}

/** @brief Tests that a profiling build on one device leaves the other devices without one. */
TEST(Small_DeviceQueueSet, TheProfilingFlagBelongsToItsDevice)
{
    InMemorySet built({0, 0});

    (void)built.set.queue(1, QueueFlavour{true, true});

    EXPECT_TRUE(built.set.has_profiling_queue(1));
    EXPECT_FALSE(built.set.has_profiling_queue(0));
}

/** @brief Tests that an index past the set has no profiling queue and builds nothing. */
TEST(Small_DeviceQueueSet, AnIndexPastTheSetHasNoProfilingQueue)
{
    InMemorySet built({0});

    EXPECT_FALSE(built.set.has_profiling_queue(built.set.count()));
    EXPECT_EQ(built.factory->builds().size(), 1u);
}

/** @brief Tests that the device line the constructor logs names the backend and the driver. */
TEST(Small_DeviceQueueSet, TheDeviceLineNamesTheBackendAndTheDriver)
{
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Info);
    InMemorySet built({0});

    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Info, "Backend: "));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Info, "Driver: in-memory 0"));
}

/** @brief Tests that the first build of a lazy queue flavour logs one DEBUG with the flavour and the build time. */
TEST(Small_DeviceQueueSet, TheFirstBuildOfALazyQueueFlavourIsLogged)
{
    InMemorySet built({0});
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Debug);

    (void)built.set.queue(0, QueueFlavour{true, true});

    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "built a queue of flavour"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, "profiling=1"));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Debug, " ms on first use"));
}

/** @brief Tests that a second request for a built flavour logs nothing. */
TEST(Small_DeviceQueueSet, ASecondRequestForABuiltFlavourLogsNothing)
{
    InMemorySet built({0});
    (void)built.set.queue(0, QueueFlavour{true, true});
    SushiRuntime::TestRunner::LogCapture capture(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                                 SushiLog::LogLevel::Debug);

    (void)built.set.queue(0, QueueFlavour{true, true});

    EXPECT_TRUE(capture.records().empty());
}
