/**************************************************************************/
/* test_partition_sessions.cpp                                            */
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
 * @file test_partition_sessions.cpp
 * @brief Tests that PartitionSessions opens one session per graph, so two graphs keep their own plans, runs and journals.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

#include <SushiRuntime/partition_session/partition_sessions.hpp>
#include <SushiRuntime/partitioning/problem_hash.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_PartitionSessions, ARegistryWithNoProfilesAndNoJournalOpensNothing)
    {
        PartitionSessionsParts parts = registry_parts({1.0, 1.0});
        parts.profiles = std::make_shared<ProfileStore>(nullptr);
        PartitionSessions sessions(std::move(parts));

        EXPECT_FALSE(sessions.is_active());
        EXPECT_EQ(sessions.open(problem_hash(chain(3, 100, 2))), nullptr);
        EXPECT_TRUE(sessions.last_report().rows.empty());
    }

    TEST(Small_PartitionSessions, TwoGraphsKeepTheirOwnPlansAndReports)
    {
        PartitionSessions sessions(registry_parts({1.0, 2.0}));
        const PartitionProblem first = chain(3, 100, 2);
        const PartitionProblem second = independent_nodes(2, 2);
        const std::shared_ptr<PartitionSession> a = sessions.open(problem_hash(first));
        const std::shared_ptr<PartitionSession> b = sessions.open(problem_hash(second));
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);

        ASSERT_TRUE(a->partition(first).has_value());
        sessions.partitioned(a);
        ASSERT_TRUE(b->partition(second).has_value());
        sessions.partitioned(b);

        EXPECT_EQ(a->problem_hash(), problem_hash(first));
        EXPECT_EQ(b->problem_hash(), problem_hash(second));
        EXPECT_EQ(a->generation(), 1u);
        EXPECT_EQ(a->report().rows[0].nodes + a->report().rows[1].nodes, 3u);
        EXPECT_EQ(b->report().rows[0].nodes + b->report().rows[1].nodes, 2u);
        EXPECT_EQ(sessions.last_report().to_text(), b->report().to_text());
    }

    TEST(Small_PartitionSessions, ARunOfOneGraphNeverReachesTheOthersLedger)
    {
        PartitionSessions sessions(registry_parts({1.0, 2.0}));
        const PartitionProblem first = chain(3, 100, 2);
        const PartitionProblem second = independent_nodes(2, 2);
        const std::shared_ptr<PartitionSession> a = sessions.open(problem_hash(first));
        const std::shared_ptr<PartitionSession> b = sessions.open(problem_hash(second));
        ASSERT_TRUE(a->partition(first).has_value());
        ASSERT_TRUE(b->partition(second).has_value());

        // A run placed by graph A's problem, handed to graph B's session, is dropped there.
        EXPECT_FALSE(b->measure(problem_hash(first), 1, 4.0, step_dispatches({{host_key(2), 4.0}})));
        EXPECT_TRUE(a->measure(problem_hash(first), 1, 4.0, step_dispatches({{host_key(2), 4.0}})));

        EXPECT_EQ(a->report().measured_step_seconds, 4.0);
        EXPECT_EQ(b->report().measured_step_seconds, 0.0);
        EXPECT_EQ(b->dropped_runs(), 1u);
        EXPECT_EQ(a->dropped_runs(), 0u);
    }

    TEST(Small_PartitionSessions, AJournalIsReplayedByTheGraphWhoseFirstProblemItOpensWith)
    {
        PartitionSession recorded = live_session({1.0, 1.0});
        ASSERT_TRUE(recorded.partition(independent_nodes(2, 2)).has_value());
        PartitionSessionsParts parts = registry_parts({1.0, 1.0});
        parts.profiles = std::make_shared<ProfileStore>(nullptr);
        PartitionSessions sessions(std::move(parts));

        ASSERT_TRUE(sessions.replay(recorded.journal().entries()).has_value());
        ASSERT_TRUE(sessions.is_active());
        // Another graph has no journal and, with no profiles, nothing plans for it.
        EXPECT_EQ(sessions.open(problem_hash(chain(3, 100, 2))), nullptr);
        const std::shared_ptr<PartitionSession> replayed = sessions.open(problem_hash(independent_nodes(2, 2)));

        ASSERT_NE(replayed, nullptr);
        ASSERT_TRUE(replayed->partition(independent_nodes(2, 2)).has_value());
        EXPECT_EQ(replayed->answer().value().node_devices, recorded.answer().value().node_devices);
    }

    TEST(Small_PartitionSessions, AReplayAfterTheGraphOpenedItsSessionIsRefused)
    {
        PartitionSession recorded = live_session({1.0, 1.0});
        ASSERT_TRUE(recorded.partition(independent_nodes(2, 2)).has_value());
        PartitionSessions sessions(registry_parts({1.0, 1.0}));
        ASSERT_NE(sessions.open(problem_hash(independent_nodes(2, 2))), nullptr);

        const Result<void> replay = sessions.replay(recorded.journal().entries());

        ASSERT_FALSE(replay.has_value());
        EXPECT_EQ(replay.error().code, ErrorCode::InvalidState);
    }

    TEST(Small_PartitionSessions, AJournalThatDoesNotOpenWithAFirstPartitionIsRefused)
    {
        PartitionSessions sessions(registry_parts({1.0, 1.0}));

        const Result<void> replay = sessions.replay({});

        ASSERT_FALSE(replay.has_value());
        EXPECT_EQ(replay.error().code, ErrorCode::InvalidArgument);
    }

    TEST(Small_PartitionSessions, AGraphsJournalFileIsNamedByItsFirstProblemAndOccurrence)
    {
        const std::filesystem::path base = std::filesystem::path("profiles") / "device_profile.txt";

        EXPECT_EQ(PartitionSessions::journal_path(base, 0xffULL, 0),
                  std::filesystem::path("profiles") / "device_profile.txt.00000000000000ff.journal");
        EXPECT_EQ(PartitionSessions::journal_path(base, 0xffULL, 2),
                  std::filesystem::path("profiles") / "device_profile.txt.00000000000000ff-2.journal");
    }

    /** @brief Fixture that owns a directory of its own, removed after the test. */
    class Medium_PartitionSessions : public ::testing::Test
    {
        protected:
            /** @brief Makes the directory. */
            void SetUp() override
            {
                directory_ = std::filesystem::temp_directory_path() / "sushiruntime_partition_sessions";
                std::filesystem::remove_all(directory_);
                std::filesystem::create_directories(directory_);
            }

            /** @brief Removes the directory. */
            void TearDown() override
            {
                std::error_code ignored;
                std::filesystem::remove_all(directory_, ignored);
            }

            /// @brief The test's own directory.
            std::filesystem::path directory_;
    };

    TEST_F(Medium_PartitionSessions, TwoGraphsWriteTwoJournalsEachHoldingItsOwnPlans)
    {
        PartitionSessionsParts parts = registry_parts({1.0, 2.0});
        const std::filesystem::path base = directory_ / "device_profile.txt";
        parts.journal_base = base;
        PartitionSessions sessions(std::move(parts));
        const PartitionProblem first = chain(3, 100, 2);
        const PartitionProblem second = independent_nodes(2, 2);
        const std::shared_ptr<PartitionSession> a = sessions.open(problem_hash(first));
        const std::shared_ptr<PartitionSession> b = sessions.open(problem_hash(second));
        ASSERT_TRUE(a->partition(first).has_value());
        ASSERT_TRUE(b->partition(second).has_value());

        const Result<std::vector<PlacementJournalEntry>> journal_a =
            PlacementJournal::read(PartitionSessions::journal_path(base, problem_hash(first), 0));
        const Result<std::vector<PlacementJournalEntry>> journal_b =
            PlacementJournal::read(PartitionSessions::journal_path(base, problem_hash(second), 0));

        ASSERT_TRUE(journal_a.has_value()) << journal_a.error().message;
        ASSERT_TRUE(journal_b.has_value()) << journal_b.error().message;
        ASSERT_EQ(journal_a.value().size(), 1u);
        ASSERT_EQ(journal_b.value().size(), 1u);
        EXPECT_EQ(journal_a.value()[0].problem_hash, problem_hash(first));
        EXPECT_EQ(journal_b.value()[0].problem_hash, problem_hash(second));
    }
} // namespace
