/**************************************************************************/
/* test_observability.cpp                                                 */
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
 * @file test_observability.cpp
 * @brief Tests the profiling breakdown, node names and transfer accounting a RunReport carries.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>
#include <algorithm>
#include <string>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    RuntimeConfig profiling_config()
    {
        RuntimeConfig cfg;
        cfg.profiling = true;
        return cfg;
    }
} // namespace

TEST(Integration_Observability, ProfilingOffLeavesBreakdownEmpty)
{
    constexpr std::size_t N = 256;

    auto rt  = Runtime::create().value();                 // no profiling
    EXPECT_FALSE(rt.advanced().is_profiling_enabled());

    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    auto rep = g.run(4).value();

    EXPECT_TRUE(rep.node_timings.empty());
    EXPECT_TRUE(rep.worker_timings.empty());
}

TEST(Integration_Observability, ProfilingOnCapturesPerNodeTiming)
{
    constexpr std::size_t N = 4096;
    constexpr std::size_t STEPS = 8;

    auto rt = Runtime::create(profiling_config()).value();
    ASSERT_TRUE(rt.advanced().is_profiling_enabled());

    auto a = rt.buffer<float>(N).value();
    auto b = rt.buffer<float>(N).value();
    float* pa = a.data();
    float* pb = b.data();

    auto g = rt.graph();
    // A heavier kernel and a lighter one, two distinct plan nodes.
    g.add(N, Reads{}, Writes(a), [pa](std::size_t i)
    {
        float v = static_cast<float>(i);
        for (int k = 0; k < 64; ++k) v = v * 1.0001f + 1.0f;
        pa[i] = v;
    });
    g.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<float>(i); });

    auto rep = g.run(STEPS).value();

    // Each kernel is one plan node per step, and only the counts are asserted; see README.md.
    ASSERT_EQ(rep.node_timings.size(), 2u);
    for (const auto& nt : rep.node_timings)
    {
        EXPECT_EQ(nt.invocations, STEPS) << "node#" << nt.plan_index;
        EXPECT_GE(nt.device_ms, 0.0);
        EXPECT_GE(nt.host_ms, 0.0);
    }
}

/** @brief Tests that a name given through name() reaches RunReport::NodeTiming. */
TEST(Integration_Observability, NamingTheLastNodeReachesNodeTiming)
{
    constexpr std::size_t N = 512;
    constexpr std::size_t STEPS = 4;

    auto rt = Runtime::create(profiling_config()).value();
    ASSERT_TRUE(rt.advanced().is_profiling_enabled());

    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf),
          [p](std::size_t i) { p[i] = static_cast<int>(i); }).name("seed");

    auto rep = g.run(STEPS).value();

    ASSERT_EQ(rep.node_timings.size(), 1u);
    EXPECT_STREQ(rep.node_timings[0].name.c_str(), "seed");
}

/** @brief Tests that every add() shape can be named, the two variadic ones included. */
TEST(Integration_Observability, EveryAddShapeCanBeNamed)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create(profiling_config()).value();
    auto a = rt.buffer<float>(N).value();
    auto b = rt.buffer<float>(N).value();
    auto c = rt.buffer<float>(N).value();
    auto s = rt.state<float>(N).value();
    float* pa = a.data();
    float* pb = b.data();

    auto g = rt.graph();

    // Plain tracked per-element.
    g.add(N, Reads(a), Writes(b),
          [pa, pb](std::size_t i) { pb[i] = pa[i] + 1.0f; }).name("plain");

    // Variadic capture shape: the pack's last element is the callable.
    g.add(Extent<1>{N}, In(b), Out(c),
          [](sycl::id<1> i, const float* in, float* out) { out[i] = in[i] * 2.0f; })
     .name("captured");

    // State shape.
    g.add(s, [](std::size_t i, const float* cur, float* next) { next[i] = cur[i] + 1.0f; })
     .name("stateful");

    // Host shape.
    g.add_host(Reads(c), Writes{}, [] {}).name("host");

    // Dynamic per-element shape.
    g.add(sized([] { return N; }), N, Reads(a), Writes(b),
          [pa, pb](std::size_t i) { pb[i] = pa[i]; }).name("dynamic");

    auto rep = g.run().value();

    std::vector<std::string> names;
    for (const auto& nt : rep.node_timings)
        names.push_back(nt.name);

    for (const char* expected : {"plain", "captured", "stateful", "host", "dynamic"})
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end())
            << "no node reported as '" << expected << "'";
}

/** @brief Tests that an unnamed node reports the documented default label. */
TEST(Integration_Observability, UnnamedNodeStillReportsTheDefaultLabel)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create(profiling_config()).value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
    auto rep = g.run().value();

    ASSERT_EQ(rep.node_timings.size(), 1u);
    EXPECT_STREQ(rep.node_timings[0].name.c_str(), "unnamed_task");
}

TEST(Integration_Observability, ProfilingOnPopulatesWorkerSplit)
{
    constexpr std::size_t N = 1024;

    // The worker split describes the scheduler, so keep this plan off the chain.
    RuntimeConfig config = profiling_config();
    config.force_scheduler = true;
    auto rt = Runtime::create(config).value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    auto rep = g.run(16).value();

    EXPECT_EQ(rep.layer, Internal::Observability::ExecutionLayer::Scheduler);
    ASSERT_FALSE(rep.worker_timings.empty());
    EXPECT_GE(rep.scheduling_overhead_ms, 0.0);

    // At least some time must be attributed somewhere across the workers.
    double total = 0.0;
    for (const auto& w : rep.worker_timings)
        total += w.busy_ms + w.stealing_ms + w.polling_ms + w.idle_ms;
    EXPECT_GT(total, 0.0);
}

TEST(Integration_Observability, ExplicitTransfersAreAccounted)
{
    constexpr std::size_t N = 512;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    // The context counts the runtime-issued copies directly.
    std::vector<int> src(N, 7);
    buf.write_range({0, N}, src).value();
    EXPECT_EQ(rt.advanced().host_to_device_bytes(), N * sizeof(int));

    auto out = buf.read_range({0, N}).value();
    ASSERT_EQ(out.size(), N);
    EXPECT_EQ(rt.advanced().device_to_host_bytes(), N * sizeof(int));
}

TEST(Integration_Observability, TransferBytesAttributedToTheRunWindow)
{
    constexpr std::size_t N = 128;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    std::vector<int> src(N, 3);

    // The bytes a host node pushes during the run land in this run's window; see README.md.
    auto g = rt.graph();
    g.add_host([&buf, &src]() { buf.write_range({0, N}, src).value(); });
    auto rep = g.run().value();

    EXPECT_EQ(rep.host_to_device_bytes, N * sizeof(int));
    EXPECT_EQ(rep.device_to_host_bytes, 0u);
}
