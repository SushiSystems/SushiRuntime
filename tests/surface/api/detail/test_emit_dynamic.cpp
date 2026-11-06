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
#include <SushiRuntime/engine_handles/late_controls_access.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the kernel runs over the live count and keeps its bindings. */
TEST(Medium_EmitDynamic, TheKernelRunsOverTheLiveCount)
{
    constexpr std::size_t K_CAPACITY = 8;
    auto runtime = Runtime::create().value();
    int* const data = Testing::shared_integers(runtime, K_CAPACITY, 0);
    ASSERT_NE(data, nullptr);
    auto bindings = std::make_shared<Internal::Graph::LateBindings>();
    bindings->count.store(5);
    const std::shared_ptr<Detail::LateControls> controls = Detail::surface_controls(bindings);
    const Detail::NodeDescription description = Detail::dynamic_node(
        controls, nullptr, {}, {BufferRegion(data)}, K_CAPACITY,
        [data](std::size_t index) { data[index] = 1; }, K_CAPACITY);
    EXPECT_EQ(description.kind, Detail::NodeKind::Dynamic);
    EXPECT_EQ(description.bindings, controls);
    Testing::run_description(runtime, description);
    for (std::size_t index = 0; index < K_CAPACITY; ++index)
        EXPECT_EQ(data[index], index < 5 ? 1 : 0) << "at " << index;
    sycl::free(data, Testing::queue_of(runtime));
}
