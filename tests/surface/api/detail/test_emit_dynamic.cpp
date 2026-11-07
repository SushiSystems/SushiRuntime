/**************************************************************************/
/* test_emit_dynamic.cpp                                                  */
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
 * @file test_emit_dynamic.cpp
 * @brief Tests dynamic_node(): a late-bound description runs over the live window it is given.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_dynamic.hpp>

#include "description_runner.hpp"
#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the dynamic-node test, which runs a description on a host runtime. */
class Small_EmitDynamic : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that the kernel runs over the live count and keeps its bindings. */
TEST_F(Small_EmitDynamic, TheKernelRunsOverTheLiveCount)
{
    constexpr std::size_t K_CAPACITY = 8;
    constexpr std::size_t K_LIVE = 5;
    auto data = runtime().buffer<int>(K_CAPACITY).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const std::shared_ptr<Detail::LateControls> controls =
        Testing::bound_controls(sized([] { return K_LIVE; }));
    const Detail::NodeDescription description = Detail::dynamic_node(
        controls, nullptr, {}, {BufferRegion(words)}, K_CAPACITY,
        [words](std::size_t index) { words[index] = 1; }, K_CAPACITY);

    description.host_form();

    EXPECT_EQ(description.kind, Detail::NodeKind::Dynamic);
    EXPECT_EQ(description.bindings, controls);
    EXPECT_EQ(TestSupport::contents(data), TestSupport::window_of<int>(K_CAPACITY, 0, K_LIVE, 1, 0));
}
