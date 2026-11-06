/**************************************************************************/
/* test_plan_access.cpp                                                   */
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
 * @file test_plan_access.cpp
 * @brief Tests engine_plan() and surface_plan(): a round trip that names one object both ways.
 * @see tests/surface/engine_handles/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/graph/compiled_graph.hpp>

#include <SushiRuntime/engine_handles/plan_access.hpp>

using namespace SushiRuntime;

/** @brief Tests that the surface name and the engine plan are one object both ways. */
TEST(Small_PlanAccess, ARoundTripNamesTheSamePlan)
{
    Internal::Graph::CompiledGraph graph;
    const Internal::Graph::CompiledGraph* const engine = &graph;

    Detail::Plan* const plan = Detail::surface_plan(&graph);
    EXPECT_EQ(Detail::engine_plan(plan), engine);

    const Detail::Plan* const read_only = Detail::surface_plan(engine);
    EXPECT_EQ(Detail::engine_plan(read_only), engine);
}
