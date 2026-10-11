/**************************************************************************/
/* test_launch_reach.cpp                                                  */
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
 * @file test_launch_reach.cpp
 * @brief Tests that the reach of a launch saturates when its origin plus its size wraps.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_launch.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Names the element type of the State launched over. */
    using Word = std::uint32_t;

    constexpr std::size_t K_WIDTH = 8;
    constexpr std::size_t K_WRAPPING_ORIGIN = SIZE_MAX - 3;
} // namespace

/** @brief Tests that a rank-1 reach is origin plus size, and SIZE_MAX when that sum wraps. */
TEST(Integration_LaunchReach, ReachSaturatesAtTheLargestSize)
{
    const sycl::range<1> size(K_WIDTH);
    const Extent<1> inside{size, sycl::id<1>(16)};
    const Extent<1> wrapping{size, sycl::id<1>(K_WRAPPING_ORIGIN)};
    const NdRange<1> tiled{size, sycl::range<1>(4), sycl::id<1>(K_WRAPPING_ORIGIN)};

    EXPECT_EQ(Detail::launch_reach(inside), 24u);
    EXPECT_EQ(Detail::launch_reach(wrapping), SIZE_MAX);
    EXPECT_EQ(Detail::launch_reach(tiled), SIZE_MAX);
    EXPECT_EQ(Detail::saturated_reach(SIZE_MAX - K_WIDTH, K_WIDTH), SIZE_MAX);
}

/** @brief Tests that an Extent whose end wraps past SIZE_MAX is refused where add() is called. */
TEST(Integration_LaunchReach, AnExtentWhoseEndWrapsIsRefusedAtAdd)
{
    auto rt = Runtime::create();
    auto field = rt.state<Word>(K_WIDTH);
    auto graph = rt.graph();
    const Extent<1> window{sycl::range<1>(K_WIDTH), sycl::id<1>(K_WRAPPING_ORIGIN)};

    graph.add(window, field, [](sycl::id<1>, const Word*, Word*) {});

    ASSERT_NE(graph.refusal(), nullptr) << "a launch that ends past SIZE_MAX was recorded";
    EXPECT_EQ(graph.refusal()->code, ErrorCode::InvalidGraph);
    EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
}
