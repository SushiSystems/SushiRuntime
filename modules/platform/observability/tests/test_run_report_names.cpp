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
 * @see modules/platform/observability/tests/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/observability/run_report.hpp>

static_assert(std::is_same<SushiRuntime::RunReport,
                           SushiRuntime::Internal::Observability::RunReport>::value,
              "RunReport must be the engine's type");
static_assert(std::is_same<SushiRuntime::NodeTiming,
                           SushiRuntime::Internal::Observability::NodeTiming>::value,
              "NodeTiming must be the engine's type");
static_assert(std::is_same<SushiRuntime::NodeEventRecord,
                           SushiRuntime::Internal::Observability::NodeEventRecord>::value,
              "NodeEventRecord must be the engine's type");
static_assert(std::is_same<SushiRuntime::WorkerTiming,
                           SushiRuntime::Internal::Observability::WorkerTiming>::value,
              "WorkerTiming must be the engine's type");
static_assert(std::is_same<SushiRuntime::ClockPairing,
                           SushiRuntime::Internal::Observability::ClockPairing>::value,
              "ClockPairing must be the engine's type");
static_assert(std::is_same<SushiRuntime::ExecutionLayer,
                           SushiRuntime::Internal::Observability::ExecutionLayer>::value,
              "ExecutionLayer must be the engine's type");
static_assert(std::is_same<decltype(SushiRuntime::NodeTiming::operation),
                           SushiRuntime::OperationID>::value,
              "NodeTiming::operation must be an OperationID");

/** @brief Tests that the public names read the same report as the engine's names. */
TEST(Unit_RunReportNames, PublicNamesAreTheReportTypes)
{
    EXPECT_EQ(&SushiRuntime::K_WIDTH_UNKNOWN,
              &SushiRuntime::Internal::Observability::K_WIDTH_UNKNOWN);

    SushiRuntime::RunReport report;
    report.layer = SushiRuntime::ExecutionLayer::Chain;
    report.node_events.push_back(SushiRuntime::NodeEventRecord());
    EXPECT_EQ(report.node_events[0].launched_width, SushiRuntime::K_WIDTH_UNKNOWN);
    EXPECT_EQ(report.layer, SushiRuntime::Internal::Observability::ExecutionLayer::Chain);
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
