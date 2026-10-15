/**************************************************************************/
/* test_emit_tracked.cpp                                                  */
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
 * @file test_emit_tracked.cpp
 * @brief Tests launch_node(), range_node() and raw_node(): the width each states and the work each runs.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_tracked.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Holds the elements every launch here covers. */
constexpr std::size_t K_COUNT = 16;

/** @brief Tests that a launch over a count runs the kernel once per index. */
TEST(Medium_EmitTracked, ALaunchRunsOncePerIndex)
{
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, K_COUNT, 0);
    ASSERT_NE(data, nullptr);
    const Detail::NodeDescription description = Detail::launch_node(
        {}, {BufferRegion(data)}, K_COUNT, [data](std::size_t index) { data[index] += static_cast<int>(index); });
    EXPECT_EQ(description.kind, Detail::NodeKind::Kernel);
    EXPECT_EQ(description.width, K_COUNT);
    Testing::run_description(runtime, description);
    for (std::size_t index = 0; index < K_COUNT; ++index)
        EXPECT_EQ(data[index], static_cast<int>(index));
    sycl::free(data, Testing::queue_of(runtime));
}

/** @brief Tests that a range kernel is handed the whole range and the handler. */
TEST(Medium_EmitTracked, ARangeKernelGetsTheWholeRange)
{
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, K_COUNT, 0);
    ASSERT_NE(data, nullptr);
    const Detail::NodeDescription description = Detail::range_node(
        {}, {BufferRegion(data)}, K_COUNT,
        [data](std::size_t begin, std::size_t end, sycl::handler& handler)
        {
            handler.single_task([data, begin, end] { data[0] = static_cast<int>(end - begin); });
        });
    EXPECT_EQ(description.width, K_COUNT);
    Testing::run_description(runtime, description);
    EXPECT_EQ(data[0], static_cast<int>(K_COUNT));
    sycl::free(data, Testing::queue_of(runtime));
}

/** @brief Tests that a raw command group keeps the width the caller states. */
TEST(Medium_EmitTracked, ARawNodeKeepsTheStatedWidth)
{
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, 1, 0);
    ASSERT_NE(data, nullptr);
    const Detail::NodeDescription description = Detail::raw_node(
        {}, {BufferRegion(data)}, [data](sycl::handler& handler) { handler.single_task([data] { data[0] = 3; }); }, 9);
    EXPECT_EQ(description.width, 9u);
    Testing::run_description(runtime, description);
    EXPECT_EQ(data[0], 3);
    sycl::free(data, Testing::queue_of(runtime));
}
