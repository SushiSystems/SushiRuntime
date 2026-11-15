/**************************************************************************/
/* partition_session.hpp                                                  */
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
 * @file partition_session.hpp
 * @brief Declares PartitionSession, one graph's plan in force, what its runs measured and its journal.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/dispatch_observation.hpp>
#include <SushiRuntime/device_profile/profile_transfer_cost.hpp>
#include <SushiRuntime/partition_session/partition_ledger.hpp>
#include <SushiRuntime/partition_session/placement_journal.hpp>
#include <SushiRuntime/partition_session/plan_source.hpp>
#include <SushiRuntime/partition_session/profile_store.hpp>
#include <SushiRuntime/partition_session/rebalance_decision.hpp>
#include <SushiRuntime/partition_session/transfer_path_tally.hpp>
#include <SushiRuntime/partitioning/partition_problem.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the parts a PartitionSession is built from, besides its plan source. */
        struct PartitionSessionParts
        {
            std::vector<PartitionDevice> devices;              /**< The runtime's devices; device i has index i. */
            std::shared_ptr<TransferPathTally> copies;         /**< Counts the copies of the runtime's paths; not null. */
            SpaceDeviceResolver device_of;                     /**< Names the device of a memory space. */
            std::optional<std::filesystem::path> journal_file; /**< File the placement journal is appended to; nothing keeps it in memory only. */
        };

        /**
         * @brief Holds one graph's plan in force, the ledger of that plan's runs and the placement journal.
         *
         * Each graph owns its session, so two graphs never share one. A session is used by its graph
         * between that graph's runs and takes no lock. Its plans come from a PlanSource: the live
         * partitioners or a journal. Every plan put in force is an entry of the journal.
         * @see docs/design/GLOBAL_HANDLE/PARTITIONING.md
         */
        class PartitionSession final
        {
            public:
                /** @brief Builds a session from its parts and the source of its plans, which is not null. */
                PartitionSession(PartitionSessionParts parts, std::unique_ptr<PlanSource> source);

                /** @brief Returns the devices of the runtime, device i at index i. */
                [[nodiscard]] const std::vector<PartitionDevice>& devices() const noexcept
                {
                    return parts_.devices;
                }

                /**
                 * @brief Takes the plan of problem from the source, puts it in force and starts a new ledger.
                 * @return The answer in force, or the source's refusal, which leaves the plan in force as it was.
                 */
                [[nodiscard]] const Result<PartitionAssignment, PartitionRefusal>& partition(PartitionProblem problem);

                /** @brief Returns whether a plan was put in force. */
                [[nodiscard]] bool has_plan() const noexcept
                {
                    return in_force_.has_value();
                }

                /**
                 * @brief Returns the answer in force.
                 * @pre has_plan().
                 */
                [[nodiscard]] const Result<PartitionAssignment, PartitionRefusal>& answer() const;

                /** @brief Returns problem_hash() of the problem in force; zero before the first plan. */
                [[nodiscard]] std::uint64_t problem_hash() const noexcept
                {
                    return in_force_ ? in_force_->problem_hash : 0;
                }

                /** @brief Returns how many times a plan was put in force; a caller that saw another number places again. */
                [[nodiscard]] std::uint64_t generation() const noexcept
                {
                    return generation_;
                }

                /**
                 * @brief Adds a run of the plan placed by problem_hash to the ledger; any other run is dropped and counted.
                 * @param steps      Steps the run took.
                 * @param seconds    Seconds those steps took.
                 * @param dispatches The dispatches the run measured; empty for an untimed run.
                 * @return Whether the run was added.
                 */
                bool measure(std::uint64_t problem_hash, std::size_t steps, double seconds,
                             const std::vector<DispatchObservation>& dispatches);

                /** @brief Returns how many runs measure() dropped because they were not of the plan in force. */
                [[nodiscard]] std::uint64_t dropped_runs() const noexcept
                {
                    return dropped_runs_;
                }

                /** @brief Returns the ledger the runs of the plan in force added to. */
                [[nodiscard]] const PartitionLedger& ledger() const noexcept
                {
                    return ledger_;
                }

                /**
                 * @brief Folds the dispatches the ledger holds unfolded into the profile file at path, through store.
                 * @return False when there was nothing to fold, or the error of the write.
                 */
                [[nodiscard]] Result<bool> fold_into(ProfileStore& store, const std::filesystem::path& path)
                {
                    return store.fold_into_file(ledger_, path);
                }

                /**
                 * @brief Hands the horizon's dispatches and the report of the plan in force to the source.
                 *
                 * The source's action is returned; every action but Keep is journalled and every change of
                 * plan raises generation(). A session with no answered plan in force keeps it.
                 */
                RebalanceAction rebalance();

                /** @brief Returns the placement journal. */
                [[nodiscard]] const PlacementJournal& journal() const noexcept
                {
                    return journal_;
                }

                /** @brief Returns the report of the plan in force with what its runs measured; empty when there is none. */
                [[nodiscard]] PartitionReport report() const;

            private:
                /** @brief Puts plan in force, clears the ledger, marks the copies counted so far and raises the generation. */
                void install(PartitionPlan plan);

                /** @brief Appends the plan in force to the journal with the numbers of judged. */
                void journal_plan(PlacementReason reason, const PartitionReport& judged);

                /** @brief Returns the copies counted since the plan in force was installed. */
                [[nodiscard]] PathTallies copies_since_install() const;

                /// @brief The parts the session was built from.
                PartitionSessionParts parts_;

                /// @brief Gives the first plan of a problem and what each horizon does.
                std::unique_ptr<PlanSource> source_;

                /// @brief The plan in force; nothing before the first.
                std::optional<PartitionPlan> in_force_;

                /// @brief Plans put in force so far.
                std::uint64_t generation_ = 0;

                /// @brief Horizons judged with an answered plan in force; each journal entry records the count.
                std::uint64_t horizons_ = 0;

                /// @brief Runs measure() dropped.
                std::uint64_t dropped_runs_ = 0;

                /// @brief What the runs of the plan in force measured.
                PartitionLedger ledger_;

                /// @brief The runtime's copy tallies when the plan in force was installed.
                PathTallies copies_at_install_;

                /// @brief Every plan put in force, in order.
                PlacementJournal journal_;

                /// @brief The refusal partition() returned last, kept so the returned reference stays valid.
                std::optional<Result<PartitionAssignment, PartitionRefusal>> refusal_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
