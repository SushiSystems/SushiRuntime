/**************************************************************************/
/* test_journal_plan_source.cpp                                           */
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
 * @file test_journal_plan_source.cpp
 * @brief Tests that JournalPlanSource answers from a journal's entries at the horizons they were made at.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <SushiRuntime/partition_session/journal_plan_source.hpp>
#include <SushiRuntime/partitioning/assignment_hash.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the journal of a live session over two equal devices that kept its plan at horizon 1 and replanned at 2. */
    std::vector<PlacementJournalEntry> recorded_journal()
    {
        PartitionSession session = live_session({1.0, 1.0});
        EXPECT_TRUE(session.partition(independent_nodes(2, 2)).has_value());
        measure_step(session, 1.0, {{host_key(1), 0.5}, {host_key(2), 0.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::Keep);
        measure_step(session, 2.5, {{host_key(1), 0.5}, {host_key(2), 2.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::Replan);
        return session.journal().entries();
    }

    TEST(Small_JournalPlanSource, TheFirstPlanIsTheOneTheFirstEntryRecords)
    {
        const std::vector<PlacementJournalEntry> entries = recorded_journal();
        JournalPlanSource source(entries, runtime_factories());

        const Result<PartitionPlan> plan = source.first(independent_nodes(2, 2));

        ASSERT_TRUE(plan.has_value());
        EXPECT_EQ(assignment_hash(plan.value().answer.value()), entries[0].assignment_hash);
        EXPECT_EQ(plan.value().model->profiles(), entries[0].profiles);
    }

    TEST(Small_JournalPlanSource, AHorizonNoEntryNamesKeepsThePlanWhateverItMeasured)
    {
        const std::vector<PlacementJournalEntry> entries = recorded_journal();
        JournalPlanSource source(entries, runtime_factories());
        const PartitionPlan in_force = source.first(independent_nodes(2, 2)).value();

        const HorizonOutcome outcome =
            source.horizon(in_force, PartitionReport{}, step_dispatches({{host_key(2), 9.0}}), 1);

        EXPECT_EQ(outcome.action, RebalanceAction::Keep);
        EXPECT_FALSE(outcome.plan.has_value());
    }

    TEST(Small_JournalPlanSource, TheHorizonAnEntryNamesPutsItsPlanInForceWithItsReason)
    {
        const std::vector<PlacementJournalEntry> entries = recorded_journal();
        JournalPlanSource source(entries, runtime_factories());
        const PartitionPlan in_force = source.first(independent_nodes(2, 2)).value();
        static_cast<void>(source.horizon(in_force, PartitionReport{}, {}, 1));

        const HorizonOutcome outcome = source.horizon(in_force, PartitionReport{}, {}, 2);

        EXPECT_EQ(outcome.action, RebalanceAction::Replan);
        ASSERT_TRUE(outcome.plan.has_value());
        EXPECT_EQ(assignment_hash(outcome.plan->answer.value()), entries[1].assignment_hash);
    }

    TEST(Small_JournalPlanSource, AFirstPlanFromAnEntryOfAnotherReasonIsRefusedNamingIt)
    {
        std::vector<PlacementJournalEntry> entries = recorded_journal();
        entries.erase(entries.begin());
        JournalPlanSource source(entries, runtime_factories());

        const Result<PartitionPlan> plan = source.first(independent_nodes(2, 2));

        ASSERT_FALSE(plan.has_value());
        EXPECT_EQ(plan.error().code, ErrorCode::InvalidState);
        EXPECT_NE(plan.error().message.find("entry 1"), std::string::npos) << plan.error().message;
    }

    TEST(Small_JournalPlanSource, AnExhaustedJournalRefusesTheNextFirstPlan)
    {
        JournalPlanSource source({}, runtime_factories());

        const Result<PartitionPlan> plan = source.first(independent_nodes(2, 2));

        ASSERT_FALSE(plan.has_value());
        EXPECT_EQ(plan.error().code, ErrorCode::InvalidState);
    }
} // namespace
