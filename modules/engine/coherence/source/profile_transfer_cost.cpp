/**************************************************************************/
/* profile_transfer_cost.cpp                                            */
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
 * @file profile_transfer_cost.cpp
 * @brief Implements select_transfer_cost().
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/profile_transfer_cost.hpp>

#include <cstddef>
#include <limits>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        TransferCost select_transfer_cost(std::shared_ptr<const CostModel> model, SpaceDeviceResolver resolver)
        {
            if (model == nullptr)
                return uniform_transfer_cost();

            return [model = std::move(model), resolver = std::move(resolver)](
                       const TransferPath& path, MemorySpaceID source, MemorySpaceID destination,
                       std::size_t bytes)
            {
                const std::optional<DeviceKey> from = resolver(source);
                const std::optional<DeviceKey> to = resolver(destination);
                if (from && to)
                {
                    const std::optional<double> seconds =
                        model->transfer_time(*from, *to, path.name(), bytes);
                    if (seconds)
                        return *seconds;
                }
                return std::numeric_limits<double>::infinity();
            };
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
