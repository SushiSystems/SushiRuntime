/**************************************************************************/
/* partition_sessions.hpp                                                 */
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
 * @file partition_sessions.hpp
 * @brief Declares PartitionSessions, which opens one PartitionSession per graph of a runtime and holds what they share.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/profile_transfer_cost.hpp>
#include <SushiRuntime/partition_session/partition_session.hpp>
#include <SushiRuntime/partition_session/partitioner_factory.hpp>
#include <SushiRuntime/partition_session/placement_journal.hpp>
#include <SushiRuntime/partition_session/profile_store.hpp>
#include <SushiRuntime/partition_session/rebalance_decision.hpp>
#include <SushiRuntime/partition_session/transfer_path_tally.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the parts every session of a runtime is opened with. */
        struct PartitionSessionsParts
        {
            std::shared_ptr<ProfileStore> profiles;          /**< The runtime's profiles; not null, and holding no model plans nothing live. */
            PartitionerFactories partitioners;               /**< Build the scheduler and the floor; no scheduler plans nothing live. */
            std::vector<PartitionDevice> devices;            /**< The runtime's devices; device i has index i. */
            std::shared_ptr<TransferPathTally> copies;       /**< Counts the copies of the runtime's paths; not null. */
            SpaceDeviceResolver device_of;                   /**< Names the device of a memory space. */
            double margin = K_DEFAULT_REBALANCE_MARGIN;      /**< How far a measured time may pass its model before a horizon acts. */
            std::optional<std::filesystem::path> journal_base; /**< The device profile file the journals sit beside; nothing keeps them in memory only. */
        };

        /**
         * @brief Opens one PartitionSession per graph and holds what the sessions share: the profiles,
         *        the journals to replay and the session that partitioned last.
         *
         * The graph owns the session it opens; this object keeps only a weak reference to the last one.
         * Two graphs may open, partition and fold at once, so every member takes the object's lock or
         * the ProfileStore's; no session's own state is shared.
         */
        class PartitionSessions final
        {
            public:
                /** @brief Builds the registry from its parts. */
                explicit PartitionSessions(PartitionSessionsParts parts);

                /** @brief Returns whether a session may plan: the profiles and a scheduler are set, or a journal waits to be replayed. */
                [[nodiscard]] bool is_active() const;

                /** @brief Returns the devices of the runtime, device i at index i. */
                [[nodiscard]] const std::vector<PartitionDevice>& devices() const noexcept
                {
                    return parts_.devices;
                }

                /** @brief Returns the runtime's profiles. */
                [[nodiscard]] ProfileStore& profiles() noexcept
                {
                    return *parts_.profiles;
                }

                /**
                 * @brief Keeps entries for the graph whose first problem hashes to the first entry's problem.
                 *
                 * The n-th graph opened for that problem takes the n-th journal kept for it.
                 * @return InvalidArgument for no entries or a first entry that is not a First one;
                 *         InvalidState when a graph with that first problem has already opened its session.
                 */
                [[nodiscard]] Result<void> replay(std::vector<PlacementJournalEntry> entries);

                /**
                 * @brief Opens the session of a graph whose first problem hashes to first_problem_hash.
                 *
                 * A journal kept for that problem makes it a replaying session; otherwise it plans live.
                 * Its journal file is journal_path() of the base, the hash and the graphs opened for it before.
                 * @return The session, which the graph owns, or null when nothing can plan for it.
                 */
                [[nodiscard]] std::shared_ptr<PartitionSession> open(std::uint64_t first_problem_hash);

                /** @brief Remembers session as the one that partitioned last, for last_report(). */
                void partitioned(const std::shared_ptr<const PartitionSession>& session);

                /** @brief Returns whether a session of this runtime has partitioned, so runs are timed for its ledger. */
                [[nodiscard]] bool has_partitioned() const noexcept
                {
                    return partitioned_.load(std::memory_order_acquire);
                }

                /** @brief Returns the report of the session that partitioned last; empty when it is gone or none did. */
                [[nodiscard]] PartitionReport last_report() const;

                /**
                 * @brief Returns the journal file of the occurrence-th graph opened with first problem hash.
                 *
                 * The name is `<base>.<hash>.journal` for the first, `<base>.<hash>-<occurrence>.journal` after,
                 * hash being sixteen lower-case hexadecimal digits; it depends on nothing else.
                 */
                [[nodiscard]] static std::filesystem::path journal_path(const std::filesystem::path& base,
                                                                        std::uint64_t first_problem_hash,
                                                                        std::uint64_t occurrence);

            private:
                /// @brief The parts the sessions are opened with.
                PartitionSessionsParts parts_;

                /// @brief Guards the members below.
                mutable std::mutex mutex_;

                /// @brief Journals waiting to be replayed, by the problem hash of their first entry, in the order kept.
                std::map<std::uint64_t, std::deque<std::vector<PlacementJournalEntry>>> replays_;

                /// @brief Sessions opened so far, by first problem hash.
                std::map<std::uint64_t, std::uint64_t> opened_;

                /// @brief The session that partitioned last.
                std::weak_ptr<const PartitionSession> last_;

                /// @brief Set by the first partitioned(); read without the lock on every run.
                std::atomic<bool> partitioned_{false};
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
