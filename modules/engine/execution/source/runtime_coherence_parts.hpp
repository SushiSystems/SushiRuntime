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
#include <SushiRuntime/memory/device_access_gate.hpp>
#include <SushiRuntime/partition_session/partition_sessions.hpp>
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
            /**
             * @brief Builds the spaces and the paths over devices, which must outlive this object.
             * @param gate Opened around each copy of every path; null for none; must outlive this object.
             */
            RuntimeCoherenceParts(Device::DeviceQueueSet& devices, Memory::DeviceAccessGate* gate);

            RuntimeCoherenceParts(const RuntimeCoherenceParts&) = delete;
            RuntimeCoherenceParts& operator=(const RuntimeCoherenceParts&) = delete;
            RuntimeCoherenceParts(RuntimeCoherenceParts&&) = delete;
            RuntimeCoherenceParts& operator=(RuntimeCoherenceParts&&) = delete;

            /**
             * @brief Builds the registry that opens each graph's partition session over model, replacing the one held.
             * @param model Null plans nothing live.
             * @param journal_base The profile file each graph's placement journal sits beside; empty keeps them in memory.
             */
            void start_partitioning(const Device::DeviceQueueSet& devices,
                                    std::shared_ptr<const Coherence::CostModel> model,
                                    const std::filesystem::path& journal_base);

            RuntimeMemorySpaces spaces;                                /**< The host's and each device's space. */
            Coherence::TransferObserverSlot transfer_observer;         /**< Forwards each copy to the observer a caller installed; outlives paths. */
            RuntimeTransferPaths paths;                                /**< The paths that copy between the spaces. */
            Coherence::TransferCost transfer_cost;                     /**< Prices one planned copy; the bytes until a profile is loaded. */
            Coherence::LogicalTimeEviction eviction;                   /**< The policy every coherence planner evicts with. */
            std::shared_ptr<Coherence::TransferPathTally> path_tally;  /**< Counts each copy by destination and path. */
            std::unique_ptr<Coherence::PartitionSessions> partitioning; /**< Opens each graph's partition session; holds the shared profiles. */
        };

        /**
         * @brief Builds the coherence parts of a runtime over devices, which must outlive them.
         * @param gate Opened around each copy of every path; null for none; must outlive the parts.
         */
        [[nodiscard]] std::unique_ptr<RuntimeCoherenceParts> make_coherence_parts(Device::DeviceQueueSet& devices,
                                                                                  Memory::DeviceAccessGate* gate);
    } // namespace Internal::Execution
} // namespace SushiRuntime
