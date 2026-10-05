/****************************************************************/
/* test_graph_profiling_switch.cpp                              */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// G1 task 4 coverage: the per-graph profiling switch (spec
// docs/superpowers/specs/2026-09-15-g1-node-event-records-design.md §B, plan
// task 4). Covers set_profiling_requested()'s independence across graphs,
// the plan's recompile-once contract, and every device-dispatch branch the
// scheduler can route to a profiling queue.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    // True once any device queue this runtime built carries enable_profiling.
    bool any_profiling_queue_built(Execution::RuntimeContext& ctx)
    {
        for (std::size_t i = 0; i < ctx.device_count(); ++i)
            if (ctx.has_profiling_queue(i))
                return true;
        return false;
    }
} // namespace

TEST(Integration_GraphProfilingSwitch, TwoGraphsIndependentSwitch)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create(); // RuntimeSettings::profiling defaults to false
    ASSERT_FALSE(rt.advanced().profiling_enabled());
    ASSERT_FALSE(any_profiling_queue_built(rt.unsafe_context()));

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    int* pa = a.data();
    int* pb = b.data();

    auto requested = rt.graph();
    requested.set_profiling_requested(true);
    EXPECT_TRUE(requested.profiling_requested());
    requested.add(API::Reads{}, API::Writes(a), N,
                  [pa](std::size_t i) { pa[i] = static_cast<int>(i); });

    auto plain = rt.graph();
    EXPECT_FALSE(plain.profiling_requested());
    plain.add(API::Reads{}, API::Writes(b), N,
              [pb](std::size_t i) { pb[i] = static_cast<int>(i); });

    // A request alone, before any run, must not build a queue.
    EXPECT_FALSE(any_profiling_queue_built(rt.unsafe_context()));

    auto rep_requested = requested.run();
    EXPECT_FALSE(rep_requested.node_timings.empty())
        << "the requested graph must collect device timing";
    EXPECT_TRUE(any_profiling_queue_built(rt.unsafe_context()))
        << "running the requested graph must have built a profiling queue";

    auto rep_plain = plain.run();
    EXPECT_TRUE(rep_plain.node_timings.empty())
        << "the unrequested graph must carry no profiling info of its own";
}

TEST(Integration_GraphProfilingSwitch, ChangedRequestRecompilesOnceAndSwitches)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto rep_off = g.run();
    EXPECT_TRUE(rep_off.node_timings.empty());
    const std::size_t compiles_after_first_run = g.compile_count();
    EXPECT_EQ(compiles_after_first_run, 1u);

    g.set_profiling_requested(true);
    auto rep_on = g.run();
    EXPECT_FALSE(rep_on.node_timings.empty());
    EXPECT_EQ(g.compile_count(), compiles_after_first_run + 1)
        << "flipping the request must recompile exactly once";

    // Running again with the same request recompiles zero further times.
    auto rep_on_again = g.run();
    EXPECT_FALSE(rep_on_again.node_timings.empty());
    EXPECT_EQ(g.compile_count(), compiles_after_first_run + 1);
}

// A node with no declared Reads/Writes leaves the scheduler's anchor unset,
// forcing the NUMA-local fallback branch instead of the cached-index one the
// two tests above exercise. Nothing public exposes which branch a node took
// (Node::cached_queue_index_ is scheduler-private), so this asserts the
// fallback's observable effect instead: a profiling queue gets built, and
// timing is collected. Were the fallback still landing on the plain queue,
// the poller's timestamp read on that event would throw, since the queue
// was never built with enable_profiling — so this also fails loudly on a
// regression rather than just missing timing silently.
//
// The remaining branch (get_queue_for_ptr for an anchor the USM registry
// does not recognise) cannot be forced safely from a test: reaching it needs
// a device kernel dereferencing a pointer the runtime never allocated,
// which is an illegal access on a real GPU backend.
TEST(Integration_GraphProfilingSwitch, NumaFallbackBranchUsesProfilingQueue)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create(); // RuntimeSettings::profiling defaults to false
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.set_profiling_requested(true);
    g.add(API::Reads{}, API::Writes{}, N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto rep = g.run();
    EXPECT_FALSE(rep.node_timings.empty())
        << "the NUMA-fallback branch must still dispatch on a profiling queue";
    EXPECT_TRUE(any_profiling_queue_built(rt.unsafe_context()))
        << "reaching this branch under profiling must build a profiling queue";
}
