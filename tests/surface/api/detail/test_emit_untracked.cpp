/**************************************************************************/
/* test_emit_untracked.cpp                                                */
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
 * @file test_emit_untracked.cpp
 * @brief Tests the untracked emitters: no regions, the device stated, and the work run.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_untracked.hpp>

#include "description_runner.hpp"
#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the test that runs an untracked description on the program's runtime. */
class Medium_EmitUntracked : public TestSupport::RuntimeTest
{
};

/** @brief Tests that an untracked launch names its device and touches no region. */
TEST_F(Medium_EmitUntracked, AnUntrackedLaunchNamesItsDevice)
{
    auto data = runtime().buffer<int>(4).value();
    TestSupport::fill_with(data, 0);
    int* const words = data.data();
    const Detail::NodeDescription description = Detail::untracked_launch_node(
        std::size_t{4}, 0, [words](std::size_t index) { words[index] = 1; });

    Testing::run_description(runtime(), description);

    EXPECT_EQ(description.kind, Detail::NodeKind::Untracked);
    EXPECT_EQ(description.device, 0u);
    EXPECT_EQ(description.width, 4u);
    EXPECT_TRUE(description.reads.empty());
    EXPECT_TRUE(description.writes.empty());
    EXPECT_EQ(TestSupport::contents(data), std::vector<int>(4, 1));
}

/** @brief Tests that an untracked range description states the width and the device it was given. */
TEST(Small_EmitUntracked, ARangeFormStatesItsWidthAndDevice)
{
    const Detail::NodeDescription range = Detail::untracked_range_node(
        8, 2, [](std::size_t, std::size_t, sycl::handler&) {});

    EXPECT_EQ(range.width, 8u);
    EXPECT_EQ(range.device, 2u);
}

/** @brief Tests that an untracked raw description is untracked and states the device it was given. */
TEST(Small_EmitUntracked, ARawFormIsUntrackedOnItsDevice)
{
    const Detail::NodeDescription raw = Detail::untracked_raw_node(1, [](sycl::handler&) {});

    EXPECT_EQ(raw.kind, Detail::NodeKind::Untracked);
    EXPECT_EQ(raw.device, 1u);
}
