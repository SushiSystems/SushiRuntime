/**************************************************************************/
/* cost_model.cpp                                                       */
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
 * @file cost_model.cpp
 * @brief Implements CostModel.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/cost_model.hpp>

#include <limits>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the seconds amount takes at rate, infinite when there is work and no rate, zero when there is no work. */
            [[nodiscard]] double seconds_for(double amount, double rate) noexcept
            {
                if (amount <= 0.0)
                    return 0.0;
                if (rate <= 0.0)
                    return std::numeric_limits<double>::infinity();
                return amount / rate;
            }
        } // namespace

        std::optional<double> CostModel::node_time(const DeviceKey& device, const NodeCost& cost) const
        {
            const DeviceProfile* found = profiles_.find(device);
            if (found == nullptr)
                return std::nullopt;
            return seconds_for(static_cast<double>(cost.operations), found->throughput_for(cost.kind)) +
                   seconds_for(static_cast<double>(cost.moved_bytes()), found->memory_bandwidth);
        }

        std::optional<double> CostModel::transfer_time(const DeviceKey& source, const DeviceKey& destination,
                                                       std::string_view path_name, std::size_t bytes) const
        {
            const DeviceProfile* from = profiles_.find(source);
            const DeviceProfile* to = profiles_.find(destination);
            const PathProfile* path = from != nullptr ? from->find_path(path_name) : nullptr;
            if (path == nullptr && to != nullptr)
                path = to->find_path(path_name);
            if (path == nullptr)
                return std::nullopt;
            return path->latency + seconds_for(static_cast<double>(bytes), path->bandwidth);
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
