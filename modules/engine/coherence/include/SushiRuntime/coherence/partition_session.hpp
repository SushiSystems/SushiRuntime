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
 * @brief Declares PartitionSession, a runtime's partitioners, the plan in force and what the runs measured.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partition_ledger.hpp>
#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/coherence/partitioner.hpp>
#include <SushiRuntime/coherence/partitioner_factory.hpp>
#include <SushiRuntime/coherence/placement_journal.hpp>
#include <SushiRuntime/coherence/profile_transfer_cost.hpp>
#include <SushiRuntime/coherence/rebalance_decision.hpp>
#include <SushiRuntime/coherence/transfer_path_tally.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/report/partition_report.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the parts a PartitionSession is built from. */
        struct PartitionSessionParts
        {
            std::shared_ptr<const CostModel> model;        /**< The cost model read at create; null when no profile file was loaded. */
            PartitionerFactories partitioners;             /**< Build the scheduler and the floor over a model; the session is inactive without the scheduler. */
            std::vector<PartitionDevice> devices;          /**< The runtime's devices; device i has index i. */
            std::shared_ptr<TransferPathTally> copies;     /**< Counts the copies of the runtime's paths; not null. */
            SpaceDeviceResolver device_of;                 /**< Names the device of a memory space. */
            double margin = K_DEFAULT_REBALANCE_MARGIN;    /**< How far a measured time may pass its model before a horizon acts. */
            std::optional<std::filesystem::path> journal_file; /**< File the placement journal is appended to; nothing keeps it in memory only. */
        };

        /**
         * @brief Holds a runtime's partitioners, the plan in force, the ledger of its runs and the placement journal.
         *
         * Plans are made, measured and rebalanced between runs, so the session takes no lock. Every plan
         * put in force is an entry of the journal.
         * @see docs/design/GLOBAL_HANDLE/PARTITIONING.md
         */
        class PartitionSession final
        {
            public:
                /** @brief Builds a session from its parts. */
                explicit PartitionSession(PartitionSessionParts parts);

                /** @brief Returns whether the session plans: it has a cost model and a scheduler, or replays a journal. */
                [[nodiscard]] bool is_active() const noexcept
                {
                    return scheduler_ != nullptr || replaying();
                }

                /** @brief Returns whether the session answers from a placement journal rather than its partitioners. */
                [[nodiscard]] bool replaying() const noexcept
                {
                    return replay_.has_value();
                }

                /**
                 * @brief Makes the session answer from entries, in order, and stop planning on its own.
                 *
                 * Each partition() takes the next entry, which must be a First entry for the same problem.
                 * A horizon takes the next entry when it is not a First entry and was made at this horizon,
                 * or carries no horizon, and puts its plan in force; any other horizon keeps the plan.
                 * @return InvalidState when a plan is already in force.
                 */
                [[nodiscard]] Result<void> replay(std::vector<PlacementJournalEntry> entries);

                /** @brief Returns the devices of the runtime, device i at index i. */
                [[nodiscard]] const std::vector<PartitionDevice>& devices() const noexcept
                {
                    return parts_.devices;
                }

                /**
                 * @brief Partitions problem with the profiles in memory, puts the answer in force and starts a new ledger.
                 * @pre is_active().
                 */
                [[nodiscard]] const Result<PartitionAssignment, PartitionRefusal>& partition(PartitionProblem problem);

                /** @brief Returns whether a plan was partitioned. */
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

                /** @brief Returns the ledger the runs of the plan in force add to. */
                [[nodiscard]] PartitionLedger& ledger() noexcept
                {
                    return ledger_;
                }

                /**
                 * @brief Folds the horizon's dispatches into the profiles in memory and judges the plan in force.
                 *
                 * A device slower than its model replans with the folded profiles; a plan slower than its
                 * floor's model runs the floor for one horizon and the faster of the two stays. Every
                 * action but Keep is journalled and every change of plan raises generation().
                 */
                RebalanceAction rebalance();

                /** @brief Returns the placement journal. */
                [[nodiscard]] const PlacementJournal& journal() const noexcept
                {
                    return journal_;
                }

                /** @brief Returns the profiles in memory: the ones read at create with every horizon folded in. */
                [[nodiscard]] const DeviceProfileSet* profiles() const noexcept
                {
                    return model_ != nullptr ? &model_->profiles() : nullptr;
                }

                /** @brief Returns the report of the plan in force with what its runs measured; empty when there is none. */
                [[nodiscard]] PartitionReport report() const;

            private:
                /** @brief Holds a problem, the answer in force for it and the model that priced the answer. */
                struct Plan
                {
                    PartitionProblem problem;                               /**< The problem. */
                    std::uint64_t problem_hash = 0;                         /**< problem_hash() of problem. */
                    Result<PartitionAssignment, PartitionRefusal> answer;   /**< The answer. */
                    std::shared_ptr<const CostModel> model;                 /**< The model the answer was priced with. */
                    PlannedBy planned_by = PlannedBy::Scheduler;            /**< The partitioner that answered. */
                };

                /** @brief Puts plan in force, clears the ledger and the copy tallies and raises the generation. */
                void install(Plan plan);

                /** @brief Appends the plan in force to the journal with the numbers of judged. */
                void journal_plan(PlacementReason reason, const PartitionReport& judged);

                /** @brief Answers problem from the next journal entry; the replay form of partition(). */
                [[nodiscard]] const Result<PartitionAssignment, PartitionRefusal>& replay_partition(PartitionProblem problem);

                /** @brief Puts the next journal entry in force when it is not a First entry; the replay form of rebalance(). */
                RebalanceAction replay_rebalance(const PartitionReport& judged);

                /** @brief Returns the plan entry gives problem, priced by the entry's profiles. */
                [[nodiscard]] Result<Plan> replay_plan(const PlacementJournalEntry& entry, PartitionProblem problem) const;

                /// @brief The parts the session was built from.
                PartitionSessionParts parts_;

                /// @brief The model in memory: the one read at create with every horizon folded in.
                std::shared_ptr<const CostModel> model_;

                /// @brief The scheduler over model_; null when the session is inactive.
                std::unique_ptr<Partitioner> scheduler_;

                /// @brief The plan in force; nothing before the first.
                std::optional<Plan> in_force_;

                /// @brief The plan a floor on trial replaced; nothing when no floor is on trial.
                std::optional<Plan> replaced_;

                /// @brief What the floor check remembers between horizons.
                RebalanceState state_;

                /// @brief Plans put in force so far.
                std::uint64_t generation_ = 0;

                /// @brief What the runs of the plan in force measured.
                PartitionLedger ledger_;

                /// @brief Every plan put in force, in order.
                PlacementJournal journal_;

                /// @brief The entries a replay answers from; nothing when the session plans on its own.
                std::optional<std::vector<PlacementJournalEntry>> replay_;

                /// @brief Horizons judged with an answered plan in force; each journal entry records the count.
                std::uint64_t horizons_ = 0;

                /// @brief The next entry of replay_ to answer from.
                std::size_t replay_next_ = 0;

                /// @brief The refusal partition() returned last in replay, kept so the returned reference stays valid.
                std::optional<Result<PartitionAssignment, PartitionRefusal>> replay_refusal_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
