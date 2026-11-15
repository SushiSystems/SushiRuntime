/**************************************************************************/
/* planned_homes.cpp                                                      */
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
 * @file planned_homes.cpp
 * @brief Implements planned_homes().
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/planned_homes.hpp>

#include <initializer_list>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        std::map<GlobalHandle, DeviceKey> planned_homes(const PartitionModel& model,
                                                        const std::vector<std::size_t>& placement)
        {
            std::map<GlobalHandle, DeviceKey> homes;
            const PartitionProblem& problem = model.problem();
            for (const bool writers : {true, false})
            {
                for (std::size_t node = 0; node < problem.nodes.size(); ++node)
                {
                    const PartitionNode& entry = problem.nodes[node];
                    for (const std::size_t buffer : writers ? entry.writes : entry.reads)
                    {
                        if (!problem.buffers[buffer].device)
                            homes.emplace(problem.buffers[buffer].handle, model.device_key(placement[node]));
                    }
                }
            }
            return homes;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
