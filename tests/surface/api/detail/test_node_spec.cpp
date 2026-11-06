/**************************************************************************/
/* test_node_spec.cpp                                                     */
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
 * @file test_node_spec.cpp
 * @brief Tests NodeSpec: it holds the description it was built from and an empty stamp.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <utility>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/node_spec.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that the spec keeps the description and starts with a default stamp. */
TEST(Small_NodeSpec, KeepsItsDescription)
{
    Detail::NodeDescription description;
    description.width = 7;
    const Detail::NodeSpec spec(std::move(description));
    EXPECT_EQ(spec.description.width, 7u);
    EXPECT_EQ(spec.stamp.width, K_WIDTH_UNKNOWN);
}
