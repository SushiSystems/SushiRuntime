/**************************************************************************/
/* placement_journal.hpp                                                  */
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
 * @file placement_journal.hpp
 * @brief Declares PlacementJournal, the record of every partition decision a session made.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/device_profile_set.hpp>
#include <SushiRuntime/handle/device_key.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names why a plan was put in force. */
        enum class PlacementReason : std::uint8_t
        {
            First,      /**< The graph was partitioned at a commit. */
            Replan,     /**< A device measured slower than its model, so its profile was folded and the graph partitioned again. */
            TryFloor,   /**< The plan measured slower than the floor's model, so the floor runs for one horizon. */
            KeepFloor,  /**< The floor on trial measured faster, so it stays. */
            LeaveFloor  /**< The floor on trial measured no faster, so the plan it replaced returns. */
        };

        /** @brief Names the partitioner that answered: the scheduler or the floor alone. */
        enum class PlannedBy : std::uint8_t
        {
            Scheduler, /**< ListSchedulingPartitioner. */
            Floor      /**< FloorPartitioner. */
        };

        /** @brief Holds one partition decision: what was answered, from which numbers, and why. */
        struct PlacementJournalEntry
        {
            std::uint64_t sequence = 0;                          /**< Place of the entry in its journal, from zero. */
            PlacementReason reason = PlacementReason::First;     /**< Why the plan was put in force. */
            PlannedBy planned_by = PlannedBy::Scheduler;         /**< The partitioner that answered. */
            std::optional<std::uint64_t> horizon;                /**< Horizons the session had passed when the plan was put in force; nothing applies at the next horizon. */
            std::uint64_t problem_hash = 0;                     /**< problem_hash() of the problem answered. */
            std::uint64_t assignment_hash = 0;                   /**< assignment_hash() of the answer. */
            double modelled_step_seconds = 0.0;                  /**< Modelled step of the plan the horizon judged. */
            double measured_step_seconds = 0.0;                  /**< Measured step of that plan; zero for First. */
            double floor_step_seconds = 0.0;                     /**< Modelled step of that plan's floor. */
            std::map<DeviceKey, double> measured_device_seconds; /**< Measured device time per step of that plan. */
            DeviceProfileSet profiles;                           /**< The profiles the answer was priced with. */

            /** @brief Returns whether every field is equal. */
            [[nodiscard]] bool operator==(const PlacementJournalEntry& other) const
            {
                return sequence == other.sequence && reason == other.reason && planned_by == other.planned_by &&
                       horizon == other.horizon && problem_hash == other.problem_hash && assignment_hash == other.assignment_hash &&
                       modelled_step_seconds == other.modelled_step_seconds &&
                       measured_step_seconds == other.measured_step_seconds &&
                       floor_step_seconds == other.floor_step_seconds &&
                       measured_device_seconds == other.measured_device_seconds && profiles == other.profiles;
            }
        };

        /**
         * @brief Holds the partition decisions of one session in memory and, when given a file, appends each to it.
         *
         * The text format is line based and versioned; modules/engine/coherence/README.md holds its schema.
         * One list of entries always gives the same bytes, and the file holds exactly serialise(entries()).
         */
        class SUSHIRUNTIME_API PlacementJournal final
        {
            public:
                /** @brief Holds the version the writer writes. */
                static constexpr int K_CURRENT_VERSION = 2;

                /** @brief Holds the oldest version the reader reads; its entries carry no horizon. */
                static constexpr int K_OLDEST_VERSION = 1;

                /** @brief Builds a journal kept in memory, and in file when one is named. */
                explicit PlacementJournal(std::optional<std::filesystem::path> file = std::nullopt);

                /**
                 * @brief Numbers entry after the last one, keeps it and appends it to the file.
                 * @return The error of the write; the entry is kept in memory either way.
                 */
                [[nodiscard]] Result<void> append(PlacementJournalEntry entry);

                /** @brief Returns the entries in the order they were appended. */
                [[nodiscard]] const std::vector<PlacementJournalEntry>& entries() const noexcept
                {
                    return entries_;
                }

                /**
                 * @brief Returns the text of entries, the version line first.
                 * @return InvalidArgument when a profile's path name is empty or holds white space.
                 */
                [[nodiscard]] static Result<std::string> serialise(const std::vector<PlacementJournalEntry>& entries);

                /** @brief Returns the entries text holds; InvalidArgument names a malformed line. */
                [[nodiscard]] static Result<std::vector<PlacementJournalEntry>> parse(std::string_view text);

                /** @brief Reads and parses the file at path; InvalidArgument when it cannot be opened. */
                [[nodiscard]] static Result<std::vector<PlacementJournalEntry>> read(const std::filesystem::path& path);

            private:
                /// @brief The file each entry is appended to; nothing for a journal kept in memory only.
                std::optional<std::filesystem::path> file_;

                /// @brief The entries, in order.
                std::vector<PlacementJournalEntry> entries_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
