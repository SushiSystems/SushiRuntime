/**************************************************************************/
/* test_step_run.cpp                                                      */
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
 * @file test_step_run.cpp
 * @brief Tests the run entries the step_run.hpp calls serve: the stop test and the idle runtime.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the tests that run graphs of a host runtime to a stop test or a cancel. */
class Small_StepRun : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that run_until takes a step for each time its stop test answers false. */
TEST_F(Small_StepRun, RunUntilStepsOncePerFalseAnswerOfItsStopTest)
{
    const Runtime& rt = runtime();
    auto steps = rt.buffer<int>(1).value();
    int* const counter = steps.data();
    counter[0] = 0;
    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(steps), [counter]() { counter[0] += 1; });
    int asked = 0;
    const auto after_two_steps = [&asked]() { return ++asked > 2; };

    graph.run_until(after_two_steps).value();

    EXPECT_EQ(counter[0], 2);
}

/** @brief Tests that a graph runs after another graph's run has returned. */
TEST_F(Small_StepRun, AGraphRunsAfterAnotherGraphsRunHasReturned)
{
    const Runtime& rt = runtime();
    auto marks = rt.buffer<int>(2).value();
    int* const words = marks.data();
    words[0] = 0;
    words[1] = 0;
    auto first = rt.graph();
    first.add_host(Reads{}, Writes(marks), [words]() { words[0] = 1; });
    auto second = rt.graph();
    second.add_host(Reads{}, Writes(marks), [words]() { words[1] = 1; });
    first.run().value();

    const auto outcome = second.run();

    EXPECT_TRUE(outcome.has_value());
    EXPECT_EQ(words[1], 1);
}

/** @brief Tests that cancelling a graph that has no step pending leaves its next run untouched. */
TEST_F(Small_StepRun, CancellingAGraphWithNoStepPendingChangesNothing)
{
    const Runtime& rt = runtime();
    auto mark = rt.buffer<int>(1).value();
    int* const word = mark.data();
    word[0] = 0;
    auto graph = rt.graph();
    graph.add_host(Reads{}, Writes(mark), [word]() { word[0] = 1; });

    graph.cancel();
    const auto outcome = graph.run();

    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(word[0], 1);
}
