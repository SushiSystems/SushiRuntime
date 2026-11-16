/**************************************************************************/
/* dispatch_observation.hpp                                               */
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
 * @file dispatch_observation.hpp
 * @brief Declares DispatchObservation, DeviceKeyTable and observe_dispatches(), the ledger's records as measured work.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/report/node_table.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the seconds one dispatch took on one device and the cost its node declared. */
        struct DispatchObservation
        {
            DeviceKey device;     /**< Device that ran the dispatch. */
            NodeCost cost;        /**< Cost the node declared for one dispatch. */
            double seconds = 0.0; /**< Seconds from the record's start to its end. */
        };

        /** @brief Names the DeviceKey behind each device index of a run, and the key of the host. */
        struct DeviceKeyTable
        {
            std::vector<DeviceKey> by_index;   /**< Key of the device with each index, as NodeEventRecord::device_index counts. */
            std::optional<DeviceKey> host;     /**< Key of the host device; empty when records of host nodes are not folded. */
        };

        /**
         * @brief Turns the records of a run into observations, in record order.
         *
         * A record is dropped when its node has no row in table, the row states no cost, the cost counts no
         * operations, the device index has no key, or the record's end does not follow its start. The
         * records are the ones ProfilingLedger::collect() appended to RunObservation::events.
         */
        [[nodiscard]] SUSHIRUNTIME_API std::vector<DispatchObservation> observe_dispatches(const std::vector<NodeEventRecord>& records,
                                                                          const NodeTable& table,
                                                                          const DeviceKeyTable& devices);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
