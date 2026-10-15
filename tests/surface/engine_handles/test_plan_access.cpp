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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_plan_access.cpp
 * @brief Tests engine_plan() and surface_plan(): a round trip that names one object, and null for null.
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
    Detail::Plan* const plan = Detail::surface_plan(&graph);
    EXPECT_EQ(Detail::engine_plan(plan), &graph);
    const Detail::Plan* const read_only = Detail::surface_plan(static_cast<const Internal::Graph::CompiledGraph*>(&graph));
    EXPECT_EQ(Detail::engine_plan(read_only), &graph);
}

/** @brief Tests that null converts to null in both directions. */
TEST(Small_PlanAccess, NullStaysNull)
{
    EXPECT_EQ(Detail::engine_plan(static_cast<Detail::Plan*>(nullptr)), nullptr);
    EXPECT_EQ(Detail::surface_plan(static_cast<Internal::Graph::CompiledGraph*>(nullptr)), nullptr);
}
