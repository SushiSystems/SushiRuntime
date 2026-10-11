/**************************************************************************/
/* test_runtime_entry.cpp                                                 */
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
 * @file test_runtime_entry.cpp
 * @brief Tests Runtime::create(), RuntimeConfig and the AdvancedControls view of a runtime.
 * @author Mustafa Garip
 * @see modules/surface/api/tests/README.md
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <limits>
#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible<Runtime>::value, "a Runtime must not copy");
static_assert(!std::is_copy_assignable<Runtime>::value, "a Runtime must not copy-assign");
static_assert(std::is_nothrow_move_constructible<Runtime>::value, "a Runtime must move");
static_assert(std::is_nothrow_move_assignable<Runtime>::value, "a Runtime must move-assign");

/** @brief Tests that one struct sets the worker count and that a view outlives its Runtime. */
TEST(Integration_RuntimeEntry, OneStructConfiguresAndTheViewOutlivesTheRuntime)
{
    RuntimeConfig config;
    config.worker_count = 2;

    std::optional<Runtime> rt(Runtime::create(config).value());
    const AdvancedControls view = rt->advanced();

    EXPECT_EQ(view.worker_pool().count, 2u);
    const std::size_t device_count = view.device_count();
    ASSERT_GE(device_count, 1u);

    rt.reset();

    EXPECT_EQ(view.device_count(), device_count);
    EXPECT_EQ(view.devices().size(), device_count);
    EXPECT_EQ(view.worker_pool().count, 2u);
}

/** @brief Tests that devices() has one entry per device, in the order DeviceIndex counts. */
TEST(Integration_RuntimeEntry, DevicesListsEveryDeviceInOrder)
{
    const Runtime rt = Runtime::create().value();
    const AdvancedControls view = rt.advanced();
    const std::vector<Internal::Device::DeviceDescription>& devices = view.devices();

    ASSERT_EQ(devices.size(), view.device_count());
    ASSERT_FALSE(devices.empty());

    Internal::Execution::RuntimeContext& context = rt.unsafe_context();
    for (std::size_t index = 0; index < devices.size(); ++index)
    {
        const std::string reported = context.devices().queue(index).get_device()
                                         .get_info<sycl::info::device::name>();
        EXPECT_EQ(devices[index].index, index);
        EXPECT_EQ(devices[index].name, reported) << "entry " << index;
    }

    auto landed = rt.buffer<int>(16).value();
    const std::optional<std::size_t> owner = context.memory().device_of(landed.data());
    ASSERT_TRUE(owner.has_value());
    EXPECT_EQ(*owner, devices.front().index);
    EXPECT_EQ(landed.device_index().value(), devices.front().index);
}

/** @brief Tests that create() with no argument yields a runtime that runs a graph. */
TEST(Integration_RuntimeEntry, DefaultConfigCreates)
{
    constexpr std::size_t COUNT = 64;

    const Runtime rt = Runtime::create().value();
    EXPECT_FALSE(rt.advanced().is_profiling_enabled());
    EXPECT_FALSE(rt.advanced().is_rebalancer_enabled());

    auto values = rt.buffer<int>(COUNT).value();
    int* data = values.data();
    for (std::size_t i = 0; i < COUNT; ++i)
        values[i] = 0;

    auto graph = rt.graph();
    graph.add(COUNT, Reads(), Writes(values), [data](std::size_t i) { data[i] = 7; });
    graph.run().value();

    for (std::size_t i = 0; i < COUNT; ++i)
        ASSERT_EQ(values[i], 7) << "element " << i;
}

/** @brief Tests that every RuntimeConfig field reaches the runtime it configures. */
TEST(Integration_RuntimeEntry, EveryFieldReachesTheRuntime)
{
    RuntimeConfig config;
    config.worker_count = 1;
    config.pinning = PinPolicy::Float;
    config.profiling = true;
    config.rebalancer = true;
    config.force_scheduler = true;

    const Runtime rt = Runtime::create(config).value();
    const AdvancedControls view = rt.advanced();
    const Internal::Execution::RuntimeSettings& settings = rt.unsafe_context().settings();

    EXPECT_EQ(view.worker_pool().count, 1u);
    EXPECT_EQ(view.worker_pool().pinning, PinPolicy::Float);
    EXPECT_TRUE(view.is_profiling_enabled());
    EXPECT_TRUE(view.is_rebalancer_enabled());
    EXPECT_TRUE(settings.force_scheduler);

    rt.set_rebalancer_enabled(false);
    EXPECT_FALSE(view.is_rebalancer_enabled());
}

/** @brief Tests that a moved Runtime keeps the context its handles were made from. */
TEST(Integration_RuntimeEntry, AMovedRuntimeKeepsItsContext)
{
    Runtime first = Runtime::create().value();
    Internal::Execution::RuntimeContext* const context = &first.unsafe_context();

    const Runtime second = std::move(first);

    EXPECT_EQ(&second.unsafe_context(), context);
}

/** @brief Tests that a caller waits for device work and copies through the device's queue. */
TEST(Integration_RuntimeEntry, ACallerWaitsAndCopiesThroughTheDeviceQueue)
{
    constexpr std::size_t K_WORDS = 256;

    const Runtime runtime = Runtime::create().value();
    const Buffer<std::uint32_t> values = runtime.buffer<std::uint32_t>(K_WORDS).value();
    std::uint32_t* words = values.data();

    sycl::queue& queue = runtime.advanced().queue(values.device_index());
    queue.parallel_for(sycl::range<1>(K_WORDS), [words](sycl::id<1> i)
    {
        words[i] = static_cast<std::uint32_t>(i[0]) + 1u;
    });
    runtime.wait().value();

    std::vector<std::uint32_t> copied(K_WORDS, 0u);
    queue.memcpy(copied.data(), words, K_WORDS * sizeof(std::uint32_t)).wait();

    for (std::size_t i = 0; i < K_WORDS; ++i)
        EXPECT_EQ(copied[i], static_cast<std::uint32_t>(i) + 1u) << "word " << i;
}

/** @brief Tests that asking for the queue of a device the runtime does not have stops. */
TEST(Integration_RuntimeEntryDeathTest, AQueueOfAnUnknownDeviceStops)
{
    const Runtime runtime = Runtime::create().value();
    const AdvancedControls controls = runtime.advanced();

    EXPECT_DEATH(static_cast<void>(controls.queue(DeviceIndex{controls.device_count()})),
                 "Device index .* is out of bounds");
}

/** @brief Tests that a buffer and a state no device can hold come back as AllocationFailed. */
TEST(Integration_RuntimeEntry, ARequestNoDeviceCanSatisfyReturnsAllocationFailed)
{
    const Runtime runtime = Runtime::create().value();
    // Half the address space in bytes: the count fits std::size_t and no device has the room.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto buffer = runtime.buffer<std::uint8_t>(count);
    ASSERT_FALSE(buffer.has_value());
    EXPECT_EQ(buffer.error().code, ErrorCode::AllocationFailed);
    EXPECT_NE(buffer.error().message.find(std::to_string(count) + " elements"),
              std::string::npos)
        << buffer.error().message;

    const auto state = runtime.state<std::uint8_t>(count);
    ASSERT_FALSE(state.has_value());
    EXPECT_EQ(state.error().code, ErrorCode::AllocationFailed);

    const auto small = runtime.buffer<std::uint8_t>(16);
    EXPECT_TRUE(small.has_value()) << "a failed allocation must leave the runtime usable";
}
