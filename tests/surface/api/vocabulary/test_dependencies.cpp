/**************************************************************************/
/* test_dependencies.cpp                                                  */
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
 * @file test_dependencies.cpp
 * @brief Tests the dependency keys: a buffer's data, a state's two fields, and a region as given.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/dependencies.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the dependency tests that name a Buffer or a State, which allocate on the program's runtime. */
class Medium_Dependencies : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a region is given as its own key and is watched by nothing. */
TEST(Small_Dependencies, ARegionIsItsOwnKeyAndHasNoObserver)
{
    int raw = 0;
    const BufferRegion region(&raw, 0, sizeof(int));

    const Reads reads(region);

    ASSERT_EQ(reads.keys.size(), 1u);
    EXPECT_EQ(reads.keys[0].base, &raw);
    EXPECT_TRUE(reads.lives.empty());
}

/** @brief Tests that a buffer reads and writes one key, the address of its data. */
TEST_F(Medium_Dependencies, ABufferReadsAndWritesTheSameKey)
{
    auto buffer = runtime().buffer<int>(4).value();

    const Reads reads(buffer);
    const Writes writes(buffer);

    ASSERT_EQ(reads.keys.size(), 1u);
    ASSERT_EQ(writes.keys.size(), 1u);
    EXPECT_EQ(reads.keys[0].base, writes.keys[0].base);
}

/** @brief Tests that a state reads its current field and writes its next field. */
TEST_F(Medium_Dependencies, AStateReadsCurrentAndWritesNext)
{
    auto state = runtime().state<int>(4).value();

    const Reads reads(state);
    const Writes writes(state);

    ASSERT_EQ(reads.keys.size(), 1u);
    ASSERT_EQ(writes.keys.size(), 1u);
    EXPECT_EQ(reads.keys[0].base, static_cast<void*>(state.current()));
    EXPECT_EQ(writes.keys[0].base, static_cast<void*>(state.next()));
}

/** @brief Tests that a typed handle is observed, so the graph can tell when it is destroyed. */
TEST_F(Medium_Dependencies, ATypedHandleIsObserved)
{
    auto buffer = runtime().buffer<int>(4).value();

    const Reads reads(buffer);

    ASSERT_EQ(reads.lives.size(), 1u);
    EXPECT_FALSE(reads.lives[0].expired());
}

/** @brief Tests that Reads collects one key per handle, in argument order. */
TEST_F(Medium_Dependencies, ReadsAndWritesCollectOneKeyEach)
{
    auto first = runtime().buffer<int>(4).value();
    auto second = runtime().buffer<int>(4).value();

    const Reads reads(first, second);
    const Writes writes(first);

    ASSERT_EQ(reads.keys.size(), 2u);
    EXPECT_EQ(reads.keys[0].base, static_cast<void*>(first.data()));
    EXPECT_EQ(reads.keys[1].base, static_cast<void*>(second.data()));
    EXPECT_EQ(writes.keys.size(), 1u);
}
