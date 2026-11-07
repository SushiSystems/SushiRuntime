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

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the captured-node test, which runs a description on a host runtime. */
class Small_EmitCaptured : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that the kernel reads one capture and writes the other at each index. */
TEST_F(Small_EmitCaptured, TheKernelReceivesEachPointer)
{
    constexpr std::size_t K_COUNT = 8;
    auto in = runtime().buffer<int>(K_COUNT).value();
    auto out = runtime().buffer<int>(K_COUNT).value();
    TestSupport::fill_with(in, 5);
    TestSupport::fill_with(out, 0);
    const Detail::NodeDescription description = Detail::captured_node(
        {BufferRegion(in.data())}, {BufferRegion(out.data())}, K_COUNT,
        std::make_tuple(static_cast<const int*>(in.data()), out.data()),
        [](std::size_t index, const int* source, int* target) { target[index] = source[index] * 2; });

    description.host_form();

    EXPECT_EQ(description.width, K_COUNT);
    EXPECT_EQ(TestSupport::contents(out), std::vector<int>(K_COUNT, 10));
}
