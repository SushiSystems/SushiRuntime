/****************************************************************/
/* test_cross_context_guard.cpp                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// §5.4 coverage: a single kernel cannot dereference USM from two non-peer device
// contexts. Rather than letting such a node hit an illegal cross-context access, the
// scheduler detects the split on the node's first dispatch and fails the run with a
// precise error. (Real cross-device execution — splitting data with the work — is a
// WP-5 concern; this guard enforces the co-location contract until then.) The check
// needs at least two device contexts, so it skips on single-device setups.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

TEST(Integration_CrossContextGuard, NodeSpanningTwoContextsFailsCleanly)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create();
    if (rt.advanced().device_count() < 2)
        GTEST_SKIP() << "needs at least two device contexts to straddle a boundary";

    // Two buffers pinned to different device queues (and thus contexts).
    auto a = rt.buffer<int>(N, DeviceIndex{0});
    auto b = rt.buffer<int>(N, DeviceIndex{1});
    int* pb = b.data();
    const int* pa = a.data();

    // A node that reads context-0 data and writes context-1 data: no single queue can
    // run it correctly.
    auto g = rt.graph();
    g.add(API::Reads(a), API::Writes(b), N,
          [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; });

    // The run fails with a clear error instead of an illegal memory access, and the
    // runtime stays usable afterward (the error-isolation contract).
    auto result = g.try_run();
    ASSERT_FALSE(result) << "a cross-context node must not run successfully";
    EXPECT_EQ(result.error().code, Core::Errc::invalid_graph);

    // A well-formed, co-located graph still runs on the same runtime afterward.
    for (std::size_t i = 0; i < N; ++i) a[i] = static_cast<int>(i);
    int* pa2 = a.data();
    auto g2 = rt.graph();
    g2.add(API::Reads{}, API::Writes(a), N, [pa2](std::size_t i) { pa2[i] *= 2; });
    auto ok = g2.try_run();
    EXPECT_TRUE(ok) << "the pool must stay re-armable after the guarded failure";
}
