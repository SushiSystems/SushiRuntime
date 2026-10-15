/**************************************************************************/
/* test_chain_plan.cpp                                                    */
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
 * @file test_chain_plan.cpp
 * @brief Tests ChainPlan and PendingEvent: an empty plan that does not chain until classified.
 * @see tests/engine/graph/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/report/dispatch_width.hpp>

#include <SushiRuntime/graph/chain_plan.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that a default plan does not chain and holds nothing. */
TEST(Small_ChainPlanShape, ADefaultPlanDoesNotChain)
{
    const ChainPlan plan;
    EXPECT_FALSE(plan.chainable);
    EXPECT_EQ(plan.queue_index, 0u);
    EXPECT_TRUE(plan.order.empty());
    EXPECT_TRUE(plan.pending_events.empty());
}

/** @brief Tests that a pending event's widths start unknown. */
TEST(Small_ChainPlanShape, APendingEventStartsWithUnknownWidths)
{
    const PendingEvent pending{nullptr, 3, sycl::event()};
    EXPECT_EQ(pending.step, 3u);
    EXPECT_EQ(pending.widths.launched, K_WIDTH_UNKNOWN);
    EXPECT_EQ(pending.widths.live, K_WIDTH_UNKNOWN);
}
