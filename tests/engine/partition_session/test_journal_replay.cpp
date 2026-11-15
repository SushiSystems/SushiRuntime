/**************************************************************************/
/* test_journal_replay.cpp                                                */
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
 * @file test_journal_replay.cpp
 * @brief Tests that a replay of a placement journal gives the placements, and the journal bytes, of the live session.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <SushiRuntime/partition_session/journal_replay.hpp>
#include <SushiRuntime/partition_session/partition_session.hpp>
#include <SushiRuntime/partitioning/assignment_hash.hpp>

#include "support/partition_problem_builder.hpp"
#include "rebalancing_parts.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the journal of a session that partitioned, replanned for a throttled device and tried the floor. */
    std::vector<PlacementJournalEntry> live_journal()
    {
        PartitionSession session(session_parts({1.0, 1.0, 1.0}));
        EXPECT_TRUE(session.partition(independent_nodes(3, 3)).has_value());
        measure_step(session, 2.5, {{host_key(1), 0.5}, {host_key(2), 0.5}, {host_key(3), 2.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::Replan);
        measure_step(session, 9.0, {{host_key(1), 0.5}, {host_key(1), 0.5}, {host_key(2), 0.5}});
        EXPECT_EQ(session.rebalance(), RebalanceAction::TryFloor);
        return session.journal().entries();
    }

    /** @brief Returns the node devices each entry replays to, in journal order. */
    std::vector<std::vector<DeviceKey>> replayed_placements(const std::vector<PlacementJournalEntry>& entries)
    {
        std::vector<std::vector<DeviceKey>> placements;
        for (const PlacementJournalEntry& entry : entries)
            placements.push_back(replay_placement(entry, independent_nodes(3, 3), runtime_factories()).value().node_devices);
        return placements;
    }

    /** @brief Returns assignment_hash() of what each entry replays to; zero for an entry that does not replay. */
    std::vector<std::uint64_t> replayed_hashes(const std::vector<PlacementJournalEntry>& entries)
    {
        std::vector<std::uint64_t> hashes;
        for (const PlacementJournalEntry& entry : entries)
        {
            const Result<PartitionAssignment> replayed =
                replay_placement(entry, independent_nodes(3, 3), runtime_factories());
            hashes.push_back(replayed.has_value() ? assignment_hash(replayed.value()) : 0);
        }
        return hashes;
    }

    /** @brief Returns the assignment hash each entry records. */
    std::vector<std::uint64_t> recorded_hashes(const std::vector<PlacementJournalEntry>& entries)
    {
        std::vector<std::uint64_t> hashes;
        for (const PlacementJournalEntry& entry : entries)
            hashes.push_back(entry.assignment_hash);
        return hashes;
    }

    TEST(Small_JournalReplay, EveryEntryReplaysToTheAssignmentItRecords)
    {
        const std::vector<PlacementJournalEntry> entries = live_journal();
        ASSERT_EQ(entries.size(), 3u);

        EXPECT_EQ(replayed_hashes(entries), recorded_hashes(entries));
    }

    TEST(Small_JournalReplay, AJournalReadBackFromItsTextGivesTheSamePlacementsAndTheSameBytes)
    {
        const std::vector<PlacementJournalEntry> live = live_journal();
        const std::string bytes = PlacementJournal::serialise(live).value();

        const std::vector<PlacementJournalEntry> read_back = PlacementJournal::parse(bytes).value();

        EXPECT_EQ(PlacementJournal::serialise(read_back).value(), bytes);
        EXPECT_EQ(replayed_placements(read_back), replayed_placements(live));
    }

    TEST(Small_JournalReplay, AnotherProblemIsRefusedWithInvalidArgument)
    {
        const std::vector<PlacementJournalEntry> entries = live_journal();

        const Result<PartitionAssignment> replayed =
            replay_placement(entries.front(), independent_nodes(2, 3), runtime_factories());

        ASSERT_FALSE(replayed.has_value());
        EXPECT_EQ(replayed.error().code, ErrorCode::InvalidArgument);
    }
} // namespace
