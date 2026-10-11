/**************************************************************************/
/* test_profiling_queue.cpp                                               */
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
 * @file test_profiling_queue.cpp
 * @brief Tests the profiling queue RuntimeContext::devices() builds against the plain queue.
 * @see modules/engine/execution/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

#include "test_helpers.hpp"
#include "test_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using SushiRuntime::Test::USMShared;

namespace
{
    /** @brief Names the out-of-order profiling queue of a device. */
    constexpr Internal::Device::QueueFlavour K_PROFILING_FLAVOUR{false, true};
} // namespace

/**
 * @brief Tests that the profiling queue carries enable_profiling and shares the plain queue's
 *        context.
 */
TEST(Integration_ProfilingQueue, ProfilingQueueAddsThePropertyAndSharesTheContext)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;

    sycl::queue& plain = context.devices().queue(0);
    EXPECT_FALSE(plain.has_property<sycl::property::queue::enable_profiling>())
        << "the plain queue must be unchanged from today's non-profiling shape";

    sycl::queue& profiling = context.devices().queue(0, K_PROFILING_FLAVOUR);
    EXPECT_TRUE(profiling.has_property<sycl::property::queue::enable_profiling>());
    EXPECT_EQ(profiling.get_context(), plain.get_context())
        << "the two queues must share one context so USM stays legal on either";
}

/** @brief Tests that no profiling queue exists before the first request. */
TEST(Integration_ProfilingQueue, NoProfilingQueueExistsBeforeTheFirstRequest)
{
    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;

    for (std::size_t i = 0; i < context.devices().count(); ++i)
        EXPECT_FALSE(context.devices().has_profiling_queue(i))
            << "device " << i << " must not have a profiling queue yet";

    (void)context.devices().queue(0, K_PROFILING_FLAVOUR);

    EXPECT_TRUE(context.devices().has_profiling_queue(0));
    for (std::size_t i = 1; i < context.devices().count(); ++i)
        EXPECT_FALSE(context.devices().has_profiling_queue(i))
            << "requesting device 0 must not build device " << i << "'s queue";
}

/** @brief Tests that RuntimeSettings::profiling builds every device's profiling queue eagerly. */
TEST(Integration_ProfilingQueue, SettingsProfilingBuildsEveryDeviceEagerly)
{
    RuntimeSettings settings;
    settings.profiling = true;
    const auto context_owner = SushiRuntime::Test::make_context({}, nullptr, nullptr, settings);
    RuntimeContext& context = *context_owner;

    for (std::size_t i = 0; i < context.devices().count(); ++i)
        EXPECT_TRUE(context.devices().has_profiling_queue(i))
            << "device " << i << "'s profiling queue must exist before any request";
}

/**
 * @brief Tests that the profiling flavour returns the plain queue itself when
 *        RuntimeSettings::profiling is on.
 */
TEST(Integration_ProfilingQueue, SettingsProfilingReturnsThePlainQueueItself)
{
    RuntimeSettings settings;
    settings.profiling = true;
    const auto context_owner = SushiRuntime::Test::make_context({}, nullptr, nullptr, settings);
    RuntimeContext& context = *context_owner;

    EXPECT_TRUE(context.devices().queue(0) == context.devices().queue(0, K_PROFILING_FLAVOUR))
        << "the profiling flavour must alias the already-profiling plain queue, not duplicate it";
}

/**
 * @brief Tests that a kernel on the profiling queue dereferences USM allocated through the plain
 *        queue.
 */
TEST(Integration_ProfilingQueue, USMFromThePlainQueueIsUsableOnTheProfilingQueue)
{
    constexpr std::size_t N = 256;

    const auto context_owner = SushiRuntime::Test::make_context();
    RuntimeContext& context = *context_owner;
    sycl::queue& plain = context.devices().queue(0);
    sycl::queue& profiling = context.devices().queue(0, K_PROFILING_FLAVOUR);

    USMShared<int> usm(plain, N);
    ASSERT_TRUE(usm);
    int* data = usm.get();
    for (std::size_t i = 0; i < N; ++i)
        data[i] = static_cast<int>(i);

    profiling.submit([&](sycl::handler& h)
    {
        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
        {
            data[idx] = data[idx] * 2;
        });
    }).wait_and_throw();

    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(data[i], static_cast<int>(i) * 2)
            << "element " << i << " was not doubled by the profiling queue's kernel";
}
