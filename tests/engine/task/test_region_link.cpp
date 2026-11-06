/**************************************************************************/
/* test_region_link.cpp                                                   */
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
 * @file test_region_link.cpp
 * @brief Tests RegionLink: clear() drops the boundary and keeps the intra count.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/task/region_link.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that clear() empties the boundary and leaves what the region baked. */
TEST(Small_RegionLink, ClearDropsTheBoundaryOnly)
{
    RegionLink link;
    link.intra_in_degree = 2;
    link.intra_leaf = true;
    link.boundary_in_degree.store(3);
    link.boundary_successors.push_back(nullptr);

    link.clear();
    EXPECT_EQ(link.boundary_in_degree.load(), 0);
    EXPECT_TRUE(link.boundary_successors.empty());
    EXPECT_EQ(link.intra_in_degree, 2);
    EXPECT_TRUE(link.intra_leaf);
}
