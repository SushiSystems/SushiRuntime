/**************************************************************************/
/* test_runtime.cpp                                                       */
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
 * @file test_runtime.cpp
 * @brief Tests creating a Runtime and what it exposes.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

using namespace SushiRuntime;

static_assert(!std::is_copy_constructible<Runtime>::value, "a Runtime must not copy");
static_assert(!std::is_copy_assignable<Runtime>::value, "a Runtime must not copy-assign");
static_assert(std::is_nothrow_move_constructible<Runtime>::value, "a Runtime must move");
static_assert(std::is_nothrow_move_assignable<Runtime>::value, "a Runtime must move-assign");

/** @brief Tests that one struct sets the worker count and that a view outlives its Runtime. */
TEST(Medium_RuntimeEntry, OneStructConfiguresAndTheViewOutlivesTheRuntime)
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
TEST(Medium_RuntimeEntry, DevicesListsEveryDeviceInOrder)
{
    const Runtime rt = Runtime::create().value();
    const AdvancedControls view = rt.advanced();
    const std::vector<DeviceDescription>& devices = view.devices();

    ASSERT_EQ(devices.size(), view.device_count());
    ASSERT_FALSE(devices.empty());

    Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(rt);
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
TEST(Medium_RuntimeEntry, DefaultConfigCreates)
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
TEST(Medium_RuntimeEntry, EveryFieldReachesTheRuntime)
{
    RuntimeConfig config;
    config.worker_count = 1;
    config.pinning = PinPolicy::Float;
    config.profiling = true;
    config.rebalancer = true;
    config.force_scheduler = true;

    const Runtime rt = Runtime::create(config).value();
    const AdvancedControls view = rt.advanced();
    const Internal::Execution::RuntimeSettings& settings =
        SushiRuntime::Internal::ContextAccess::context(rt).settings();

    EXPECT_EQ(view.worker_pool().count, 1u);
    EXPECT_EQ(view.worker_pool().pinning, PinPolicy::Float);
    EXPECT_TRUE(view.is_profiling_enabled());
    EXPECT_TRUE(view.is_rebalancer_enabled());
    EXPECT_TRUE(settings.force_scheduler);

    rt.set_rebalancer_enabled(false);
    EXPECT_FALSE(view.is_rebalancer_enabled());
}

/** @brief Tests that a moved Runtime keeps the context its handles were made from. */
TEST(Medium_RuntimeEntry, AMovedRuntimeKeepsItsContext)
{
    Runtime first = Runtime::create().value();
    Internal::Execution::RuntimeContext* const context =
        &SushiRuntime::Internal::ContextAccess::context(first);

    const Runtime second = std::move(first);

    EXPECT_EQ(&SushiRuntime::Internal::ContextAccess::context(second), context);
}

/** @brief Tests that a caller waits for device work and copies through the device's queue. */
TEST(Medium_RuntimeEntry, ACallerWaitsAndCopiesThroughTheDeviceQueue)
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
TEST(Medium_RuntimeEntryDeathTest, AQueueOfAnUnknownDeviceStops)
{
    const Runtime runtime = Runtime::create().value();
    const AdvancedControls controls = runtime.advanced();

    EXPECT_DEATH(static_cast<void>(controls.queue(DeviceIndex{controls.device_count()})),
                 "Device index .* is out of bounds");
}

/** @brief Tests that a buffer and a state no device can hold come back as AllocationFailed. */
TEST(Medium_RuntimeEntry, ARequestNoDeviceCanSatisfyReturnsAllocationFailed)
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

/** @brief Tests that the live allocation count rises with a buffer and falls when it is freed. */
TEST(Medium_RuntimeEntry, LiveAllocationCountFollowsABuffer)
{
    auto rt = Runtime::create().value();
    const AdvancedControls controls = rt.advanced();
    const std::size_t before = controls.live_allocation_count();

    {
        const auto buf = rt.buffer<int>(8).value();
        EXPECT_EQ(controls.live_allocation_count(), before + 1);

        const auto field = rt.state<int>(8).value();
        EXPECT_EQ(controls.live_allocation_count(), before + 3) << "a State holds two fields";
    }

    EXPECT_EQ(controls.live_allocation_count(), before);
}

/**
 * @brief Tests that a thread that outlives the Runtime it allocated from tears down without a
 *        fault.
 */
TEST(Medium_Runtime, AThreadOutlivingTheRuntimeTearsDownCleanly)
{
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool allocated = false;
    bool runtime_gone = false;

    std::thread outliver;

    {
        auto rt = SushiRuntime::Runtime::create().value();

        outliver = std::thread([&]()
        {
            // The thread's exit-time destructors touch per-thread runtime state; see README.md.
            {
                auto scratch = rt.buffer<int>(4096).value();
                for (std::size_t i = 0; i < 64; ++i)
                    scratch[i] = static_cast<int>(i);
            }

            {
                std::lock_guard<std::mutex> lock(gate_mutex);
                allocated = true;
            }
            gate.notify_one();

            // Stay alive until the Runtime — and its allocator — are gone.
            std::unique_lock<std::mutex> lock(gate_mutex);
            gate.wait(lock, [&] { return runtime_gone; });
        });

        std::unique_lock<std::mutex> lock(gate_mutex);
        gate.wait(lock, [&] { return allocated; });
    }   // Runtime destroyed here, while the other thread is still alive.

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        runtime_gone = true;
    }
    gate.notify_one();

    // Reaching the line after this join without a fault is the assertion; see README.md.
    outliver.join();
    SUCCEED() << "a thread unwound after the Runtime it allocated from was gone";
}
