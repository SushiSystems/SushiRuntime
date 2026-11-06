/**************************************************************************/
/* test_in_memory_placement_memory.cpp                                    */
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
 * @file test_in_memory_placement_memory.cpp
 * @brief Tests what InMemoryPlacementMemory adds to the PlacementMemory contract: its context numbers.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/task/in_memory_placement_memory.hpp>

using SushiRuntime::Internal::Graph::InMemoryPlacementMemory;

/** @brief Tests that two devices in two contexts do not share one. */
TEST(Small_InMemoryPlacementMemory, TwoDevicesInTwoContextsDoNotShareOne)
{
    const InMemoryPlacementMemory memory(std::vector<std::size_t>{0, 1});
    EXPECT_FALSE(memory.shares_context(0, 1));
}
