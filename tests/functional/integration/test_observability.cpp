/**************************************************************************/
/* test_observability.cpp                                                 */
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

// §5.5 coverage: RunReport observability. Profiling is opt-in at construction; off,
// the report carries no profiling breakdown; on, it carries per-node and per-worker
// timing. Explicit host<->device transfers are accounted regardless of profiling.

#include <cstddef>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

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

    auto rt  = Runtime::create();                 // no profiling
    EXPECT_FALSE(rt.advanced().profiling_enabled());

    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    auto rep = g.run(4);

    EXPECT_TRUE(rep.node_timings.empty());
    EXPECT_TRUE(rep.worker_timings.empty());
}

TEST(Integration_Observability, ProfilingOnCapturesPerNodeTiming)
{
    constexpr std::size_t N = 4096;
    constexpr std::size_t STEPS = 8;

    auto rt = Runtime::create({}, profiling_config());
    ASSERT_TRUE(rt.advanced().profiling_enabled());

    auto a = rt.buffer<float>(N);
    auto b = rt.buffer<float>(N);
    float* pa = a.data();
    float* pb = b.data();

    auto g = rt.graph();
    // A heavier kernel and a lighter one, two distinct plan nodes.
    g.add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i)
    {
        float v = static_cast<float>(i);
        for (int k = 0; k < 64; ++k) v = v * 1.0001f + 1.0f;
        pa[i] = v;
    });
    g.add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] = static_cast<float>(i); });

    auto rep = g.run(STEPS);

    // Both kernels appear as distinct plan nodes, each dispatched once per step. The
    // exact device-time magnitudes are backend- and noise-dependent, so the stable
    // claim is structural: the per-node breakdown is present and correctly counted.
    ASSERT_EQ(rep.node_timings.size(), 2u);
    for (const auto& nt : rep.node_timings)
    {
        EXPECT_EQ(nt.invocations, STEPS) << "node#" << nt.plan_index;
        EXPECT_GE(nt.device_ms, 0.0);
        EXPECT_GE(nt.host_ms, 0.0);
    }
}

// §18 R8: the ordinary add() overloads (not just add_offload) can carry a caller-
// supplied diagnostic name that reaches RunReport::NodeTiming. Before this, every
// node reported as "unnamed_task" regardless of what add() call produced it.
TEST(Integration_Observability, NamedNodeAppearsInNodeTiming)
{
    constexpr std::size_t N = 512;
    constexpr std::size_t STEPS = 4;

    auto rt = Runtime::create({}, profiling_config());
    ASSERT_TRUE(rt.advanced().profiling_enabled());

    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::sized([] { return N; }), API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<int>(i); }, "named_dynamic_node");

    auto rep = g.run(STEPS);

    ASSERT_EQ(rep.node_timings.size(), 1u);
    EXPECT_STREQ(rep.node_timings[0].name.c_str(), "named_dynamic_node");
    EXPECT_NE(rep.node_timings[0].name, "unnamed_task");
}

// A node added without a name keeps the documented default, so an unlabeled call
// site is still visible in the report rather than silently absent.
TEST(Integration_Observability, UnnamedNodeStillReportsTheDefaultLabel)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create({}, profiling_config());
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    auto rep = g.run();

    ASSERT_EQ(rep.node_timings.size(), 1u);
    EXPECT_STREQ(rep.node_timings[0].name.c_str(), "unnamed_task");
}

TEST(Integration_Observability, ProfilingOnPopulatesWorkerSplit)
{
    constexpr std::size_t N = 1024;

    auto rt = Runtime::create({}, profiling_config());
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    auto rep = g.run(16);

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

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    // The context counts the runtime-issued copies directly.
    std::vector<int> src(N, 7);
    buf.write_range({0, N}, src.data());
    EXPECT_EQ(rt.advanced().host_to_device_bytes(), N * sizeof(int));

    auto out = buf.read_range({0, N});
    ASSERT_EQ(out.size(), N);
    EXPECT_EQ(rt.advanced().device_to_host_bytes(), N * sizeof(int));
}

TEST(Integration_Observability, TransferBytesAttributedToTheRunWindow)
{
    constexpr std::size_t N = 128;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    std::vector<int> src(N, 3);

    // A host node that pushes data during the run: the bytes land in this run's
    // window, not before it.
    auto g = rt.graph();
    g.add_host([&buf, &src]() { buf.write_range({0, N}, src.data()); });
    auto rep = g.run();

    EXPECT_EQ(rep.host_to_device_bytes, N * sizeof(int));
    EXPECT_EQ(rep.device_to_host_bytes, 0u);
}
