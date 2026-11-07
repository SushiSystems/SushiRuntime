/**************************************************************************/
/* test_emit_device_sized.cpp                                             */
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
 * @file test_emit_device_sized.cpp
 * @brief Tests device_sized_node(): the launch covers the capacity and stops at the live width.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_device_sized.hpp>

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the device-sized test, which runs the host form of a description on a host runtime. */
class Small_EmitDeviceSized : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that only the indices below the device-written live width run. */
TEST_F(Small_EmitDeviceSized, OnlyTheLiveIndicesRun)
{
    constexpr std::size_t K_CAPACITY = 8;
    constexpr std::size_t K_LIVE = 3;
    auto data = runtime().buffer<int>(K_CAPACITY).value();
    auto live = runtime().buffer<std::uint32_t>(2).value();
    TestSupport::fill_with(data, 0);
    live[0] = K_LIVE;
    live[1] = 0;
    DeviceSized launch;
    launch.capacity = K_CAPACITY;
    launch.live_width = live.data();
    launch.live_words = live.data();
    int* const words = data.data();
    const Detail::NodeDescription description = Detail::device_sized_node(
        {}, {BufferRegion(words)}, launch, [words](std::size_t index) { words[index] = 1; },
        K_CAPACITY);

    description.host_form();

    EXPECT_EQ(description.width, K_CAPACITY);
    EXPECT_EQ(TestSupport::contents(data), TestSupport::window_of<int>(K_CAPACITY, 0, K_LIVE, 1, 0));
}
