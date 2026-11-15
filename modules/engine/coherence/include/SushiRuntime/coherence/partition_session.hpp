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
 * @brief Declares PartitionSession, a runtime's partitioner, its last answer and what the runs measured.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <SushiRuntime/coherence/cost_model.hpp>
#include <SushiRuntime/coherence/partition_ledger.hpp>
#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/coherence/partitioner.hpp>
#include <SushiRuntime/coherence/profile_transfer_cost.hpp>
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
            std::shared_ptr<const CostModel> model;        /**< The frozen cost model; null when no profile file was loaded. */
            std::unique_ptr<Partitioner> partitioner;      /**< Prices with model; null when model is. */
            std::vector<PartitionDevice> devices;          /**< The runtime's devices; device i has index i. */
            std::shared_ptr<TransferPathTally> copies;     /**< Counts the copies of the runtime's paths; not null. */
            SpaceDeviceResolver device_of;                 /**< Names the device of a memory space. */
        };

        /**
         * @brief Holds a runtime's partitioner, the problem and answer of its last plan and the ledger of its runs.
         *
         * Plans are made and runs measured between runs, so the session takes no lock.
         */
        class PartitionSession final
        {
            public:
                /** @brief Builds a session from its parts. */
                explicit PartitionSession(PartitionSessionParts parts);

                /** @brief Returns whether the session has a partitioner, that is a cost model to price with. */
                [[nodiscard]] bool is_active() const noexcept
                {
                    return parts_.partitioner != nullptr;
                }

                /** @brief Returns the devices of the runtime, device i at index i. */
                [[nodiscard]] const std::vector<PartitionDevice>& devices() const noexcept
                {
                    return parts_.devices;
                }

                /**
                 * @brief Partitions problem, keeps it and the answer as the last plan and starts a new ledger.
                 * @pre is_active().
                 */
                [[nodiscard]] const Result<PartitionAssignment, PartitionRefusal>& partition(PartitionProblem problem);

                /** @brief Returns whether a plan was partitioned. */
                [[nodiscard]] bool has_plan() const noexcept
                {
                    return last_.has_value();
                }

                /** @brief Returns the ledger the runs of the last plan add to. */
                [[nodiscard]] PartitionLedger& ledger() noexcept
                {
                    return ledger_;
                }

                /** @brief Returns the report of the last plan with what its runs measured; empty when there is none. */
                [[nodiscard]] PartitionReport report() const;

            private:
                /** @brief Holds a problem and the partitioner's answer to it. */
                struct Plan
                {
                    PartitionProblem problem;                               /**< The problem. */
                    Result<PartitionAssignment, PartitionRefusal> answer;   /**< The answer. */
                };

                /// @brief The parts the session was built from.
                PartitionSessionParts parts_;

                /// @brief The last plan; nothing before the first.
                std::optional<Plan> last_;

                /// @brief What the runs of the last plan measured.
                PartitionLedger ledger_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
