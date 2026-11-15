/**************************************************************************/
/* test_placement_journal.cpp                                             */
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
 * @file test_placement_journal.cpp
 * @brief Tests the placement journal's text round trip, its refusal of a bad line and its file.
 * @see tests/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <SushiRuntime/partition_session/placement_journal.hpp>

#include "support/partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns an entry of reason with every field set, its numbers not round in binary. */
    PlacementJournalEntry sample_entry(PlacementReason reason, PlannedBy planned_by)
    {
        PlacementJournalEntry entry;
        entry.reason = reason;
        entry.planned_by = planned_by;
        entry.problem_hash = 0x0123456789abcdefULL;
        entry.assignment_hash = 0x00000000000000ffULL;
        entry.modelled_step_seconds = 0.1;
        entry.measured_step_seconds = 1.0 / 3.0;
        entry.floor_step_seconds = 0.7;
        entry.measured_device_seconds.emplace(host_key(1), 0.2);
        entry.measured_device_seconds.emplace(host_key(2), 2.0 / 3.0);
        entry.profiles = speed_model({1.0, 0.3})->profiles();
        return entry;
    }

    /** @brief Returns the entries of a journal kept in memory that was given a replan and a floor trial. */
    std::vector<PlacementJournalEntry> two_decisions()
    {
        PlacementJournal journal;
        EXPECT_TRUE(journal.append(sample_entry(PlacementReason::Replan, PlannedBy::Scheduler)).has_value());
        PlacementJournalEntry floor = sample_entry(PlacementReason::TryFloor, PlannedBy::Floor);
        floor.horizon = 3;
        EXPECT_TRUE(journal.append(std::move(floor)).has_value());
        return journal.entries();
    }

    /** @brief Returns text with every line that opens with prefix removed. */
    std::string without_lines(const std::string& text, const std::string& prefix)
    {
        std::istringstream lines(text);
        std::string kept;
        std::string line;
        while (std::getline(lines, line))
            if (line.rfind(prefix, 0) != 0)
                kept += line + '\n';
        return kept;
    }

    TEST(Small_PlacementJournal, TheWriterWritesVersionTwoWithTheHorizonOfEachEntry)
    {
        const std::string text = PlacementJournal::serialise(two_decisions()).value();

        EXPECT_EQ(text.rfind("placement_journal_version 2\n", 0), 0u) << text;
        EXPECT_NE(text.find("horizon next\n"), std::string::npos) << text;
        EXPECT_NE(text.find("horizon 3\n"), std::string::npos) << text;
    }

    TEST(Small_PlacementJournal, AVersionOneJournalIsReadWithEveryEntryAtTheNextHorizon)
    {
        std::string text = without_lines(PlacementJournal::serialise(two_decisions()).value(), "horizon ");
        text.replace(0, std::string("placement_journal_version 2").size(), "placement_journal_version 1");

        const Result<std::vector<PlacementJournalEntry>> parsed = PlacementJournal::parse(text);

        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        ASSERT_EQ(parsed.value().size(), 2u);
        EXPECT_FALSE(parsed.value()[0].horizon.has_value());
        EXPECT_FALSE(parsed.value()[1].horizon.has_value());
        EXPECT_EQ(parsed.value()[1].reason, PlacementReason::TryFloor);
    }

    TEST(Small_PlacementJournal, AVersionAboveTheReadersIsRefusedNamingBothVersions)
    {
        std::string text = PlacementJournal::serialise(two_decisions()).value();
        text.replace(0, std::string("placement_journal_version 2").size(), "placement_journal_version 3");

        const Result<std::vector<PlacementJournalEntry>> parsed = PlacementJournal::parse(text);

        ASSERT_FALSE(parsed.has_value());
        EXPECT_EQ(parsed.error().code, ErrorCode::Unsupported);
        EXPECT_NE(parsed.error().message.find("version 3, this reader knows versions 1 to 2"), std::string::npos)
            << parsed.error().message;
    }

    TEST(Small_PlacementJournal, AppendNumbersTheEntriesInOrder)
    {
        const std::vector<PlacementJournalEntry> entries = two_decisions();

        ASSERT_EQ(entries.size(), 2u);
        EXPECT_EQ(entries[0].sequence, 0u);
        EXPECT_EQ(entries[1].sequence, 1u);
    }

    TEST(Small_PlacementJournal, ParsingTheTextGivesTheSameEntriesAndWritingThemGivesTheSameBytes)
    {
        const std::vector<PlacementJournalEntry> entries = two_decisions();
        const std::string text = PlacementJournal::serialise(entries).value();

        const Result<std::vector<PlacementJournalEntry>> parsed = PlacementJournal::parse(text);

        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        EXPECT_EQ(parsed.value(), entries);
        EXPECT_EQ(PlacementJournal::serialise(parsed.value()).value(), text);
    }

    TEST(Small_PlacementJournal, AnUnknownReasonIsRefusedWithItsLine)
    {
        std::string text = PlacementJournal::serialise(two_decisions()).value();
        text.replace(text.find("replan"), 6, "gossip");

        const Result<std::vector<PlacementJournalEntry>> parsed = PlacementJournal::parse(text);

        ASSERT_FALSE(parsed.has_value());
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("line 2"), std::string::npos) << parsed.error().message;
    }

    /** @brief Fixture that owns a directory of its own, removed after the test. */
    class Medium_PlacementJournal : public ::testing::Test
    {
        protected:
            /** @brief Makes the directory. */
            void SetUp() override
            {
                directory_ = std::filesystem::temp_directory_path() / "sushiruntime_placement_journal";
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

    TEST_F(Medium_PlacementJournal, TheFileHoldsExactlyTheTextOfTheEntriesAppended)
    {
        const std::filesystem::path path = directory_ / "device_profile.txt.journal";
        PlacementJournal journal(path);
        ASSERT_TRUE(journal.append(sample_entry(PlacementReason::First, PlannedBy::Scheduler)).has_value());
        ASSERT_TRUE(journal.append(sample_entry(PlacementReason::LeaveFloor, PlannedBy::Scheduler)).has_value());

        std::ifstream file(path, std::ios::binary);
        std::ostringstream bytes;
        bytes << file.rdbuf();

        EXPECT_EQ(bytes.str(), PlacementJournal::serialise(journal.entries()).value());
        EXPECT_EQ(PlacementJournal::read(path).value(), journal.entries());
    }
} // namespace
