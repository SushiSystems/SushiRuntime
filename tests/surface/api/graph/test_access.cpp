/**************************************************************************/
/* test_access.cpp                                                        */
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
 * @file test_access.cpp
 * @brief Tests Access: the regions it keeps in order and the observers of both sets.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/graph/access.hpp>

using namespace SushiRuntime;

/** @brief Tests that regions given directly keep their order and carry no observer. */
TEST(Medium_Access, RegionsKeepTheirOrder)
{
    int first = 0;
    int second = 0;
    const Access access({BufferRegion(&first), BufferRegion(&second)}, {BufferRegion(&second)});
    ASSERT_EQ(access.reads.size(), 2u);
    EXPECT_EQ(access.reads[0].base, &first);
    EXPECT_EQ(access.reads[1].base, &second);
    ASSERT_EQ(access.writes.size(), 1u);
    EXPECT_TRUE(access.lives.empty());
}

/** @brief Tests that handles given through Reads and Writes bring the observers of both. */
TEST(Medium_Access, HandlesBringTheirObservers)
{
    auto runtime = Runtime::create().value();
    auto in = runtime.buffer<int>(4).value();
    auto out = runtime.buffer<int>(4).value();
    const Access access{Reads(in), Writes(out)};
    EXPECT_EQ(access.reads.size(), 1u);
    EXPECT_EQ(access.writes.size(), 1u);
    EXPECT_EQ(access.lives.size(), 2u);
}
