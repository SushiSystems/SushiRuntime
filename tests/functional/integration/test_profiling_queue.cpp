/**************************************************************************/
/* test_profiling_queue.cpp                                              */
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

// G1 task 3 coverage: RuntimeContext::profiling_queue() (spec
// docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md §A/F2).
// A profiled node needs SYCL command_submit/start/end, which only a queue
// built with enable_profiling exposes; the plain queue returned by
// get_queue() must stay exactly as it is today for every existing consumer.

#include <cstddef>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using SushiRuntime::Test::UsmShared;

// With RuntimeSettings::profiling off (today's default), the plain queue
// carries no enable_profiling property, the profiling queue does once built,
// and the two share one sycl::context so USM allocated through the plain
// queue stays legally accessible from the profiling queue.
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

// Nothing is built before the first request: RuntimeSettings::profiling is
// off, so probe_hardware() must not have built any device's profiling queue.
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

// RuntimeSettings::profiling asks probe_hardware() to build every device's
// profiling queue eagerly, before any run() could have requested one.
TEST(Integration_ProfilingQueue, SettingsProfilingBuildsEveryDeviceEagerly)
{
    RuntimeSettings settings;
    settings.profiling = true;
    RuntimeContext ctx({}, nullptr, nullptr, settings);

    for (std::size_t i = 0; i < ctx.device_count(); ++i)
        EXPECT_TRUE(ctx.has_profiling_queue(i))
            << "device " << i << "'s profiling queue must exist before any request";
}

// With RuntimeSettings::profiling on, the plain queue already carries
// enable_profiling (today's existing behaviour), so profiling_queue() must
// hand back that same queue handle rather than a second, duplicate one.
TEST(Integration_ProfilingQueue, SettingsProfilingReturnsThePlainQueueItself)
{
    RuntimeSettings settings;
    settings.profiling = true;
    RuntimeContext ctx({}, nullptr, nullptr, settings);

    EXPECT_TRUE(ctx.get_queue(0) == ctx.profiling_queue(0))
        << "profiling_queue() must alias the already-profiling plain queue, not duplicate it";
}

// A USM allocation made through the plain queue's context is dereferenced
// correctly by a kernel dispatched on the profiling queue: the shared context
// is not just reported, it is usable.
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
