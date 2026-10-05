/****************************************************************/
/* test_observability.cpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// §5.5 coverage: RunReport observability. Profiling is opt-in at construction; off,
// the report carries no profiling breakdown; on, it carries per-node and per-worker
// timing. Explicit host<->device transfers are accounted regardless of profiling.

#include <cstddef>
#include <vector>
#include <algorithm>
#include <string>
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

// §18 R8: any node can be given a diagnostic name that reaches
// RunReport::NodeTiming. Before this, every node reported as "unnamed_task"
// regardless of which add() produced it, so a profile of a real graph was a list
// of identical labels.
//
// name_last() rather than a trailing argument: two of the add() shapes take a
// trailing parameter pack whose last element is the callable, so a defaulted
// trailing name is not expressible there. One spelling that works for every
// shape beats a trailing argument on most of them and an exception for the rest.
TEST(Integration_Observability, NamingTheLastNodeReachesNodeTiming)
{
    constexpr std::size_t N = 512;
    constexpr std::size_t STEPS = 4;

    auto rt = Runtime::create({}, profiling_config());
    ASSERT_TRUE(rt.advanced().profiling_enabled());

    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<int>(i); }).name_last("seed");

    auto rep = g.run(STEPS);

    ASSERT_EQ(rep.node_timings.size(), 1u);
    EXPECT_STREQ(rep.node_timings[0].name.c_str(), "seed");
}

// Every launch shape, including the two variadic ones that cannot take a
// trailing argument. This is the test that would have caught R8 shipping for the
// Dynamic overloads only.
TEST(Integration_Observability, EveryAddShapeCanBeNamed)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create({}, profiling_config());
    auto a = rt.buffer<float>(N);
    auto b = rt.buffer<float>(N);
    auto c = rt.buffer<float>(N);
    auto s = rt.state<float>(N);
    float* pa = a.data();
    float* pb = b.data();

    auto g = rt.graph();

    // Plain tracked per-element.
    g.add(API::Reads(a), API::Writes(b), N,
          [pa, pb](std::size_t i) { pb[i] = pa[i] + 1.0f; }).name_last("plain");

    // Variadic capture shape: the pack's last element is the callable.
    g.add(API::Extent<1>{N}, API::In(b), API::Out(c),
          [](sycl::id<1> i, const float* in, float* out) { out[i] = in[i] * 2.0f; })
     .name_last("captured");

    // State shape.
    g.add(s, [](std::size_t i, const float* cur, float* next) { next[i] = cur[i] + 1.0f; })
     .name_last("stateful");

    // Host shape.
    g.add_host(API::Reads(c), API::Writes{}, [] {}).name_last("host");

    // Dynamic per-element shape.
    g.add(API::sized([] { return N; }), API::Reads(a), API::Writes(b), N,
          [pa, pb](std::size_t i) { pb[i] = pa[i]; }).name_last("dynamic");

    auto rep = g.run();

    std::vector<std::string> names;
    for (const auto& nt : rep.node_timings)
        names.push_back(nt.name);

    for (const char* expected : {"plain", "captured", "stateful", "host", "dynamic"})
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end())
            << "no node reported as '" << expected << "'";
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

    // The worker split describes the scheduler, so keep this plan off the chain.
    RuntimeConfig config = profiling_config();
    config.force_scheduler = true;
    auto rt = Runtime::create({}, config);
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
    auto rep = g.run(16);

    EXPECT_EQ(rep.layer, Core::ExecutionLayer::Scheduler);
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
