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
#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Holds the elements every launch here covers. */
    constexpr std::size_t K_COUNT = 16;

    /** @brief Fixture of the tests that run a tracked description on the program's runtime. */
    class Medium_EmitTracked : public TestSupport::RuntimeTest
    {
    };
} // namespace

/** @brief Tests the kind and the width of a launch description. */
TEST(Small_EmitTracked, ALaunchNodeStatesItsKindAndWidth)
{
    int target = 0;

    const Detail::NodeDescription description = Detail::launch_node(
        {}, {BufferRegion(&target)}, K_COUNT, [](std::size_t) {});

    EXPECT_EQ(description.kind, Detail::NodeKind::Kernel);
    EXPECT_EQ(description.width, K_COUNT);
}

/** @brief Tests that a launch over a count runs the kernel once per index. */
TEST_F(Medium_EmitTracked, ALaunchRunsOncePerIndex)
{
    auto data = runtime().buffer<int>(K_COUNT).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const Detail::NodeDescription description = Detail::launch_node(
        {}, {BufferRegion(words)}, K_COUNT,
        [words](std::size_t index) { words[index] += static_cast<int>(index); });

    Testing::run_description(runtime(), description);

    EXPECT_EQ(TestSupport::contents(data), TestSupport::ramp<int>(K_COUNT));
}

/** @brief Tests that a range description states the width it was given. */
TEST(Small_EmitTracked, ARangeNodeStatesItsWidth)
{
    int target = 0;

    const Detail::NodeDescription description = Detail::range_node(
        {}, {BufferRegion(&target)}, K_COUNT, [](std::size_t, std::size_t, sycl::handler&) {});

    EXPECT_EQ(description.width, K_COUNT);
}

/** @brief Tests that a range kernel is handed the whole range and the handler. */
TEST_F(Medium_EmitTracked, ARangeKernelGetsTheWholeRange)
{
    auto data = runtime().buffer<int>(1).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const Detail::NodeDescription description = Detail::range_node(
        {}, {BufferRegion(words)}, K_COUNT,
        [words](std::size_t begin, std::size_t end, sycl::handler& handler)
        {
            handler.single_task([words, begin, end] { words[0] = static_cast<int>(end - begin); });
        });

    Testing::run_description(runtime(), description);

    EXPECT_EQ(TestSupport::contents(data), std::vector<int>{static_cast<int>(K_COUNT)});
}

/** @brief Tests that a raw command group keeps the width the caller states. */
TEST(Small_EmitTracked, ARawNodeKeepsTheStatedWidth)
{
    int target = 0;

    const Detail::NodeDescription description = Detail::raw_node(
        {}, {BufferRegion(&target)}, [](sycl::handler&) {}, 9);

    EXPECT_EQ(description.width, 9u);
}

/** @brief Tests that the command group of a raw description runs. */
TEST_F(Medium_EmitTracked, ARawNodeRunsItsCommandGroup)
{
    auto data = runtime().buffer<int>(1).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const Detail::NodeDescription description = Detail::raw_node(
        {}, {BufferRegion(words)},
        [words](sycl::handler& handler) { handler.single_task([words] { words[0] = 3; }); }, 9);

    Testing::run_description(runtime(), description);

    EXPECT_EQ(TestSupport::contents(data), std::vector<int>{3});
}
