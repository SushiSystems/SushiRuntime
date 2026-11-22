/**************************************************************************/
/* test_chained_run_mark.cpp                                              */
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
 * @file test_chained_run_mark.cpp
 * @brief Tests that a ChainedRunMark begins one chained run and ends it exactly once.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/execution/chained_run_mark.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace
{
    using SushiRuntime::Internal::Execution::ChainedRunMark;
    using SushiRuntime::Internal::Execution::ExecutionEngine;
    using SushiRuntime::Internal::Graph::CompiledGraph;

    /** @brief Counts the chained runs it is told of and runs nothing else. */
    class CountingEngine final : public ExecutionEngine
    {
        public:
            /** @brief Runs nothing. */
            SushiRuntime::Result<void> execute(CompiledGraph&) override
            {
                return {};
            }

            /** @brief Starts nothing. */
            SushiRuntime::Result<void> submit(CompiledGraph&) override
            {
                return {};
            }

            /** @brief Waits for nothing. */
            SushiRuntime::Result<void> complete() override
            {
                return {};
            }

            /** @brief Stops nothing. */
            void shutdown() override
            {

            }

            /** @brief Counts a chained run begun. */
            void begin_chained_run() noexcept override
            {
                ++begun;
            }

            /** @brief Counts a chained run ended. */
            void end_chained_run() noexcept override
            {
                ++ended;
            }

            int begun = 0; /**< Chained runs begun. */
            int ended = 0; /**< Chained runs ended. */
    };
} // namespace

/** @brief Tests that a mark begins a chained run when made and ends it when destroyed. */
TEST(Small_ChainedRunMark, MarksTheRunFromItsMakingToItsEnd)
{
    CountingEngine engine;
    {
        ChainedRunMark mark(engine);

        EXPECT_EQ(engine.begun, 1);
        EXPECT_EQ(engine.ended, 0);
    }

    EXPECT_EQ(engine.ended, 1);
}

/** @brief Tests that a released mark ends the run once, neither again on a second release nor at its end. */
TEST(Small_ChainedRunMark, ARunReleasedEarlyEndsOnce)
{
    CountingEngine engine;
    {
        ChainedRunMark mark(engine);
        mark.release();

        EXPECT_EQ(engine.ended, 1);
        mark.release();
    }

    EXPECT_EQ(engine.begun, 1);
    EXPECT_EQ(engine.ended, 1);
}
