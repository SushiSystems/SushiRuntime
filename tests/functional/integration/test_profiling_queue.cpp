/****************************************************************/
/* test_profiling_queue.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_profiling_queue.cpp
 * @brief Tests RuntimeContext::profiling_queue() and its relation to the plain queue.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using SushiRuntime::Test::UsmShared;

/**
 * @brief Tests that the profiling queue carries enable_profiling and shares the plain queue's
 *        context.
 */
TEST(Integration_ProfilingQueue, ProfilingQueueAddsThePropertyAndSharesTheContext)
{
    RuntimeContext ctx;

    sycl::queue& plain = ctx.get_queue(0);
    EXPECT_FALSE(plain.has_property<sycl::property::queue::enable_profiling>())
        << "the plain queue must be unchanged from today's non-profiling shape";

    sycl::queue& profiling = ctx.profiling_queue(0);
    EXPECT_TRUE(profiling.has_property<sycl::property::queue::enable_profiling>());
    EXPECT_EQ(profiling.get_context(), plain.get_context())
        << "the two queues must share one context so USM stays legal on either";
}

/** @brief Tests that no profiling queue exists before the first request. */
TEST(Integration_ProfilingQueue, NoProfilingQueueExistsBeforeTheFirstRequest)
{
    RuntimeContext ctx;

    for (std::size_t i = 0; i < ctx.device_count(); ++i)
        EXPECT_FALSE(ctx.has_profiling_queue(i))
            << "device " << i << " must not have a profiling queue yet";

    ctx.profiling_queue(0);

    EXPECT_TRUE(ctx.has_profiling_queue(0));
    for (std::size_t i = 1; i < ctx.device_count(); ++i)
        EXPECT_FALSE(ctx.has_profiling_queue(i))
            << "requesting device 0 must not build device " << i << "'s queue";
}

/** @brief Tests that RuntimeSettings::profiling builds every device's profiling queue eagerly. */
TEST(Integration_ProfilingQueue, SettingsProfilingBuildsEveryDeviceEagerly)
{
    RuntimeSettings settings;
    settings.profiling = true;
    RuntimeContext ctx({}, nullptr, nullptr, settings);

    for (std::size_t i = 0; i < ctx.device_count(); ++i)
        EXPECT_TRUE(ctx.has_profiling_queue(i))
            << "device " << i << "'s profiling queue must exist before any request";
}

/**
 * @brief Tests that profiling_queue() returns the plain queue itself when
 *        RuntimeSettings::profiling is on.
 */
TEST(Integration_ProfilingQueue, SettingsProfilingReturnsThePlainQueueItself)
{
    RuntimeSettings settings;
    settings.profiling = true;
    RuntimeContext ctx({}, nullptr, nullptr, settings);

    EXPECT_TRUE(ctx.get_queue(0) == ctx.profiling_queue(0))
        << "profiling_queue() must alias the already-profiling plain queue, not duplicate it";
}

/**
 * @brief Tests that a kernel on the profiling queue dereferences USM allocated through the plain
 *        queue.
 */
TEST(Integration_ProfilingQueue, UsmFromThePlainQueueIsUsableOnTheProfilingQueue)
{
    constexpr std::size_t N = 256;

    RuntimeContext ctx;
    sycl::queue& plain = ctx.get_queue(0);
    sycl::queue& profiling = ctx.profiling_queue(0);

    UsmShared<int> usm(plain, N);
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
