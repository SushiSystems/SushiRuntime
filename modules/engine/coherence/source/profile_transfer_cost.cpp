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

#include <atomic>
#include <cstddef>
#include <limits>
#include <utility>

#include <SushiRuntime/coherence/coherence_log_category.hpp>
#include <SushiRuntime/log/logger.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /**
             * @brief Returns the modelled seconds of one copy, or nothing when no measurement covers it.
             *
             * Two devices take the path's own measurement, else the copy through host memory. A device
             * and the host side take the device's host link in that direction.
             */
            [[nodiscard]] std::optional<double> modelled_seconds(const CostModel& model, const TransferPath& path,
                                                                 const std::optional<DeviceKey>& from,
                                                                 const std::optional<DeviceKey>& to,
                                                                 std::size_t bytes)
            {
                if (from && to)
                {
                    const std::optional<double> direct = model.transfer_time(*from, *to, path.name(), bytes);
                    return direct ? direct : model.host_staged_time(*from, *to, bytes);
                }
                if (to)
                    return model.host_copy_time(*to, HostDirection::ToDevice, bytes);
                if (from)
                    return model.host_copy_time(*from, HostDirection::ToHost, bytes);
                return std::nullopt;
            }
        } // namespace

        TransferCost select_transfer_cost(std::shared_ptr<const CostModel> model, SpaceDeviceResolver resolver)
        {
            if (model == nullptr)
                return uniform_transfer_cost();

            auto logged = std::make_shared<std::atomic<bool>>(false);
            return [model = std::move(model), resolver = std::move(resolver), logged](
                       const TransferPath& path, MemorySpaceID source, MemorySpaceID destination,
                       std::size_t bytes)
            {
                const std::optional<double> seconds =
                    modelled_seconds(*model, path, resolver(source), resolver(destination), bytes);
                if (seconds)
                    return *seconds;
                if (!logged->exchange(true, std::memory_order_relaxed))
                    SR_LOG_WARN(K_COHERENCE_LOG_CATEGORY,
                                "The device profile measures no link for a copy over {} from space {} to space {}; "
                                "such copies cost infinity. This is logged once.",
                                path.name(), source.value(), destination.value());
                return std::numeric_limits<double>::infinity();
            };
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
