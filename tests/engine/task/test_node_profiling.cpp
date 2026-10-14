/**************************************************************************/
/* test_node_profiling.cpp                                                */
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
 * @file test_node_profiling.cpp
 * @brief Tests NodeProfiling: three counters that start at zero and add up.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/task/node_profiling.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that the counters start at zero and accumulate. */
TEST(Unit_NodeProfiling, CountersStartAtZeroAndAccumulate)
{
    NodeProfiling profiling;
    EXPECT_EQ(profiling.invocations.load(), 0u);
    EXPECT_EQ(profiling.device_ns.load(), 0u);
    EXPECT_EQ(profiling.host_ns.load(), 0u);
    profiling.invocations.fetch_add(2);
    profiling.device_ns.fetch_add(500);
    EXPECT_EQ(profiling.invocations.load(), 2u);
    EXPECT_EQ(profiling.device_ns.load(), 500u);
}
