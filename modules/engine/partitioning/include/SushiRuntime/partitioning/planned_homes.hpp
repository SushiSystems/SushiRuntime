/**************************************************************************/
/* planned_homes.hpp                                                      */
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
 * @file planned_homes.hpp
 * @brief Declares planned_homes(), the home device of each planned buffer under a placement.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/partitioning/partition_model.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Returns the node that names the home of every planned buffer a node of problem touches.
         *
         * It is the buffer's first writer in recording order, or its first reader when no node writes
         * it. A buffer with a stated device has no entry. The model and planned_homes() both use it.
         */
        [[nodiscard]] std::map<GlobalHandle, std::size_t> planned_home_nodes(const PartitionProblem& problem);

        /**
         * @brief Returns the home device of every planned buffer a node of model touches.
         *
         * The home is the device of the buffer's planned_home_nodes() entry under placement.
         * @param placement Device index of each node of model.
         */
        [[nodiscard]] std::map<GlobalHandle, DeviceKey> planned_homes(const PartitionModel& model,
                                                                      const std::vector<std::size_t>& placement);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
