/**************************************************************************/
/* test_emit_captured.cpp                                                 */
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
 * @file test_emit_captured.cpp
 * @brief Tests captured_node(): the kernel receives one pointer per capture.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_captured.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the kernel reads one capture and writes the other at each index. */
TEST(Medium_EmitCaptured, TheKernelReceivesEachPointer)
{
    constexpr std::size_t K_COUNT = 8;
    auto runtime = Runtime::create().value();
    int* const in = Testing::shared_integers(runtime, K_COUNT, 5);
    int* const out = Testing::shared_integers(runtime, K_COUNT, 0);
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    const Detail::NodeDescription description = Detail::captured_node(
        {BufferRegion(in)}, {BufferRegion(out)}, K_COUNT,
        std::make_tuple(static_cast<const int*>(in), out),
        [](std::size_t index, const int* source, int* target) { target[index] = source[index] * 2; });
    EXPECT_EQ(description.width, K_COUNT);
    Testing::run_description(runtime, description);
    for (std::size_t index = 0; index < K_COUNT; ++index)
        EXPECT_EQ(out[index], 10);
    sycl::free(in, Testing::queue_of(runtime));
    sycl::free(out, Testing::queue_of(runtime));
}
