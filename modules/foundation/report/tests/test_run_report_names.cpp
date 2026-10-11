/**************************************************************************/
/* test_run_report_names.cpp                                              */
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
 * @file test_run_report_names.cpp
 * @brief Tests that the report types are reachable under their public names.
 * @see modules/foundation/report/tests/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/report/run_report.hpp>

static_assert(std::is_same<decltype(SushiRuntime::NodeTiming::operation),
                           SushiRuntime::OperationID>::value,
              "NodeTiming::operation must be an OperationID");

/** @brief Tests that a default report reads as a successful scheduler run with no rows. */
TEST(Unit_RunReportNames, ADefaultReportIsEmpty)
{
    const SushiRuntime::RunReport report;
    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(report.layer, SushiRuntime::ExecutionLayer::Scheduler);
    EXPECT_TRUE(report.node_events.empty());
    EXPECT_EQ(SushiRuntime::NodeEventRecord().launched_width, SushiRuntime::K_WIDTH_UNKNOWN);
}

/** @brief Tests that a NodeTiming names no operation until it is given one. */
TEST(Unit_RunReportNames, ANodeTimingNamesItsOperation)
{
    using namespace SushiRuntime::Literals;

    SushiRuntime::NodeTiming timing;
    EXPECT_FALSE(timing.operation.is_named());

    timing.operation = "report.names"_op;
    EXPECT_TRUE(timing.operation == "report.names"_op);
}
