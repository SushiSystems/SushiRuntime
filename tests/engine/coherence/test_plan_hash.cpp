/**************************************************************************/
/* test_plan_hash.cpp                                                     */
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
 * @file test_plan_hash.cpp
 * @brief Tests that plan_hash() depends on every field of a plan and on nothing else.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/plan_hash.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns a plan of one copy step. */
    CopyPlan one_copy()
    {
        PlannedStep step;
        step.kind = PlannedStepKind::Copy;
        step.handle = GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, 1, 7);
        step.space = MemorySpaceID(2);
        step.source = MemorySpaceID(1);
        step.end = 64;
        step.node = 3;
        CopyPlan plan;
        plan.steps.push_back(step);
        return plan;
    }

    TEST(Small_PlanHash, EqualPlansHashEqual)
    {
        EXPECT_EQ(plan_hash(one_copy()), plan_hash(one_copy()));
    }

    TEST(Small_PlanHash, TheHashOfAnEmptyPlanIsStable)
    {
        EXPECT_EQ(plan_hash(CopyPlan{}), plan_hash(CopyPlan{}));
        EXPECT_NE(plan_hash(CopyPlan{}), plan_hash(one_copy()));
    }

    TEST(Small_PlanHash, EachFieldOfAStepChangesTheHash)
    {
        const std::uint64_t base = plan_hash(one_copy());

        CopyPlan changed = one_copy();
        changed.steps[0].kind = PlannedStepKind::Free;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].space = MemorySpaceID(3);
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].source = MemorySpaceID(3);
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].begin = 8;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].end = 32;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].path = 1;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].node = 4;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].evicts = true;
        EXPECT_NE(plan_hash(changed), base);

        changed = one_copy();
        changed.steps[0].handle = GlobalHandle(1, 0, HandleKind::Buffer, HandleFlags::None, 1, 8);
        EXPECT_NE(plan_hash(changed), base);
    }

    TEST(Small_PlanHash, TheOrderOfStepsAndTheSteadyFlagChangeTheHash)
    {
        CopyPlan two = one_copy();
        PlannedStep second = two.steps[0];
        second.node = 9;
        two.steps.push_back(second);

        CopyPlan swapped = two;
        std::swap(swapped.steps[0], swapped.steps[1]);
        EXPECT_NE(plan_hash(two), plan_hash(swapped));

        CopyPlan unsteady = two;
        unsteady.steady = false;
        EXPECT_NE(plan_hash(two), plan_hash(unsteady));
    }
}
