/**************************************************************************/
/* runtime_coherence_parts.hpp                                            */
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
 * @file runtime_coherence_parts.hpp
 * @brief Declares RuntimeCoherenceParts, the spaces, paths, cost, eviction and partitioner of a runtime.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <filesystem>
#include <memory>

#include <SushiRuntime/coherence/logical_time_eviction.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device_profile/cost_model.hpp>
#include <SushiRuntime/partition_session/partition_session.hpp>
#include <SushiRuntime/partition_session/transfer_path_tally.hpp>
#include <SushiRuntime/transfer/transfer_cost.hpp>
#include <SushiRuntime/transfer/transfer_observer_slot.hpp>

#include "runtime_memory_spaces.hpp"
#include "runtime_transfer_paths.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Owns what a runtime plans copies with: the memory spaces, the transfer paths and the
         *        observer they report to, the copy cost, the eviction policy and the partitioner.
         *
         * A new planning subsystem is one more member here and one more line in the constructor;
         * RuntimeContext keeps one pointer to this object. The paths borrow the devices, so the
         * object is destroyed before the device set drains.
         */
        struct RuntimeCoherenceParts
        {
            /** @brief Builds the spaces and the paths over devices, which must outlive this object. */
            explicit RuntimeCoherenceParts(Device::DeviceQueueSet& devices);

            RuntimeCoherenceParts(const RuntimeCoherenceParts&) = delete;
            RuntimeCoherenceParts& operator=(const RuntimeCoherenceParts&) = delete;
            RuntimeCoherenceParts(RuntimeCoherenceParts&&) = delete;
            RuntimeCoherenceParts& operator=(RuntimeCoherenceParts&&) = delete;

            /**
             * @brief Builds the partition session over model, replacing the one held.
             * @param model Null leaves the session inactive.
             * @param journal_file The file the placement journal is appended to; empty keeps it in memory.
             */
            void start_partitioning(const Device::DeviceQueueSet& devices,
                                    std::shared_ptr<const Coherence::CostModel> model,
                                    const std::filesystem::path& journal_file);

            RuntimeMemorySpaces spaces;                                /**< The host's and each device's space. */
            Coherence::TransferObserverSlot transfer_observer;         /**< Forwards each copy to the observer a caller installed; outlives paths. */
            RuntimeTransferPaths paths;                                /**< The paths that copy between the spaces. */
            Coherence::TransferCost transfer_cost;                     /**< Prices one planned copy; the bytes until a profile is loaded. */
            Coherence::LogicalTimeEviction eviction;                   /**< The policy every coherence planner evicts with. */
            std::shared_ptr<Coherence::TransferPathTally> path_tally;  /**< Counts each copy by destination and path. */
            std::unique_ptr<Coherence::PartitionSession> partitioning; /**< The partitioner, its last plan and its ledger. */
        };

        /** @brief Builds the coherence parts of a runtime over devices, which must outlive them. */
        [[nodiscard]] std::unique_ptr<RuntimeCoherenceParts> make_coherence_parts(Device::DeviceQueueSet& devices);
    } // namespace Internal::Execution
} // namespace SushiRuntime
