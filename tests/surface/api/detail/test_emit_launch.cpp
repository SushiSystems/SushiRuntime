/**************************************************************************/
/* test_emit_launch.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_emit_launch.cpp
 * @brief Tests the reach a launch emitter computes for an extent.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of the State launched over. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 8;
    constexpr std::size_t K_WRAPPING_ORIGIN = SIZE_MAX - 3;
} // namespace

/** @brief Tests that a rank-1 reach is origin plus size when that sum fits. */
TEST(Small_LaunchReach, ReachIsOriginPlusSizeWhenItFits)
{
    const Extent<1> inside{sycl::range<1>(K_WIDTH), sycl::id<1>(16)};

    EXPECT_EQ(Detail::launch_reach(inside), 24u);
}

/** @brief Tests that a rank-1 reach saturates at SIZE_MAX when origin plus size wraps. */
TEST(Small_LaunchReach, ReachSaturatesAtTheLargestSizeWhenTheSumWraps)
{
    const sycl::range<1> size(K_WIDTH);
    const Extent<1> wrapping{size, sycl::id<1>(K_WRAPPING_ORIGIN)};
    const NdRange<1> tiled{size, sycl::range<1>(4), sycl::id<1>(K_WRAPPING_ORIGIN)};

    EXPECT_EQ(Detail::launch_reach(wrapping), SIZE_MAX);
    EXPECT_EQ(Detail::launch_reach(tiled), SIZE_MAX);
    EXPECT_EQ(Detail::saturated_reach(SIZE_MAX - K_WIDTH + 1u, K_WIDTH), SIZE_MAX);
}

/** @brief Tests that a launch that reaches past its handle is refused with InvalidGraph. */
TEST(Small_LaunchReach, ALaunchPastItsHandleIsRefused)
{
    const Extent<1> wrapping{sycl::range<1>(K_WIDTH), sycl::id<1>(K_WRAPPING_ORIGIN)};

    const std::optional<Error> refusal =
        Detail::launch_refusal(wrapping, K_WIDTH, "add_rule", 0, "the State");

    ASSERT_TRUE(refusal.has_value());
    EXPECT_EQ(refusal->code, ErrorCode::InvalidGraph);
}

/** @brief Tests that a launch that fits its handle is not refused. */
TEST(Small_LaunchReach, ALaunchInsideItsHandleIsNotRefused)
{
    const Extent<1> inside{sycl::range<1>(K_WIDTH), sycl::id<1>(0)};

    EXPECT_FALSE(Detail::launch_refusal(inside, K_WIDTH, "add_rule", 0, "the State").has_value());
}

/** @brief Fixture of the test that records a rule on a State of a host runtime. */
class Small_LaunchReachRecord : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that a refused launch leaves no rule recorded. */
TEST_F(Small_LaunchReachRecord, ARefusedLaunchRecordsNoRule)
{
    const Runtime& rt = runtime();
    auto field = rt.state<Word>(K_WIDTH).value();
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_WRAPPING_ORIGIN)};

    graph.add_rule(window, field, [](sycl::id<1>, const Word*, Word*) {});

    ASSERT_NE(graph.refusal(), nullptr) << "a launch that ends past SIZE_MAX was recorded";
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}
