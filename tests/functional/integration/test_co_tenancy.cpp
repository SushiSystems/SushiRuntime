/****************************************************************/
/* test_co_tenancy.cpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for the co-tenancy contract: the runtime is a tenant of the machine,
// not its owner.
//
// Inside a game the runtime shares the machine with a main thread, a render
// thread, an audio callback and asset I/O. Taking every core and pinning worker 0
// onto the same core as the host's main thread is what a batch runtime does; a
// host has to be able to say "these cores are mine". These tests assert the
// mechanism that makes that possible — a worker count, a core range, and the
// option to not pin at all — and that a narrower pool still executes correctly.

#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // Runs a small graph and checks every element, so a configuration is proven to
    // execute rather than merely to construct.
    void expect_runs_correctly(API::Runtime& rt)
    {
        constexpr std::size_t N = 512;

        auto buf = rt.buffer<int>(N);
        for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
        int* p = buf.data();

        auto g = rt.graph();
        g.add(API::Reads{}, API::Writes(buf), N,
              [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        g.run();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
    }

    std::size_t machine_cores()
    {
        const std::size_t cores = std::thread::hardware_concurrency();
        return cores == 0 ? 1u : cores;
    }
}

// The default is the previous sole-tenant behaviour: one worker per logical core,
// starting at core 0. Changing that silently would move every existing consumer's
// performance profile.
TEST(Integration_CoTenancy, DefaultsToOneWorkerPerCoreFromCoreZero)
{
    auto rt = Runtime::create();
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, machine_cores());
    EXPECT_EQ(pool.first_core, 0u);
    EXPECT_EQ(pool.pinning, API::PinPolicy::Pin);
}

// A host that wants cores 0..(reserved-1) for its own threads asks for a pool that
// starts past them. Both the count and the placement must be exactly what was
// requested — a runtime that quietly took more cores would defeat the point.
TEST(Integration_CoTenancy, ReservesTheCoresTheHostAskedFor)
{
    const std::size_t cores = machine_cores();
    if (cores < 4)
        GTEST_SKIP() << "needs at least 4 logical cores to reserve a range";

    const std::size_t reserved = 2;

    API::RuntimeConfig config;
    config.worker_count = cores - reserved;
    config.first_core   = reserved;

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, cores - reserved);
    EXPECT_EQ(pool.first_core, reserved);

    expect_runs_correctly(rt);
}

// A pool much smaller than the machine still runs everything correctly: the graph
// does not depend on there being a worker per core.
TEST(Integration_CoTenancy, ASingleWorkerStillExecutesTheWholeGraph)
{
    API::RuntimeConfig config;
    config.worker_count = 1;

    auto rt = Runtime::create({}, config);
    EXPECT_EQ(rt.advanced().worker_pool().count, 1u);

    expect_runs_correctly(rt);
}

// Float leaves placement to the OS. The pool is still the requested size; what
// changes is that no worker is bound, which is the right choice inside a container
// whose CPU allowance is enforced by quota rather than by affinity.
TEST(Integration_CoTenancy, FloatPolicyRunsUnpinned)
{
    API::RuntimeConfig config;
    config.worker_count = 2;
    config.pinning      = API::PinPolicy::Float;

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, 2u);
    EXPECT_EQ(pool.pinning, API::PinPolicy::Float);

    expect_runs_correctly(rt);
}

// A core range that runs off the end of the machine is clamped, not obeyed. The
// OS affinity calls accept an out-of-range mask and simply do not bind, so
// without the clamp the runtime would report a reservation it never made.
TEST(Integration_CoTenancy, AnOutOfRangeCoreRangeIsClampedNotIgnored)
{
    const std::size_t cores = machine_cores();

    API::RuntimeConfig config;
    config.worker_count = 4;
    config.first_core   = cores + 8;   // entirely past the last core

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_LE(pool.first_core + pool.count, cores)
        << "the worker core range must fit inside the machine";
    EXPECT_GE(pool.count, 1u) << "clamping must not leave an empty pool";

    expect_runs_correctly(rt);
}
