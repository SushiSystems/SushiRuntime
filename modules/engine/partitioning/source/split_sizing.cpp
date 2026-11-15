/**************************************************************************/
/* split_sizing.cpp                                                       */
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
 * @file split_sizing.cpp
 * @brief Defines size_split().
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/split_sizing.hpp>

#include <cmath>
#include <optional>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the speed of each device, zero for one that cannot run the node. */
            [[nodiscard]] std::vector<double> speeds_of(const std::vector<double>& device_times)
            {
                std::vector<double> speeds(device_times.size(), 0.0);
                bool any_timed = false;
                for (std::size_t device = 0; device < device_times.size(); ++device)
                {
                    const double time = device_times[device];
                    if (std::isfinite(time) && time > 0.0)
                    {
                        speeds[device] = 1.0 / time;
                        any_timed = true;
                    }
                }
                if (any_timed)
                    return speeds;
                for (std::size_t device = 0; device < device_times.size(); ++device)
                    speeds[device] = std::isfinite(device_times[device]) ? 1.0 : 0.0;
                return speeds;
            }
        } // namespace

        std::vector<SizedPart> size_split(std::uint32_t axis, std::uint64_t extent,
                                          const std::vector<double>& device_times)
        {
            const std::vector<double> speeds = speeds_of(device_times);
            double total = 0.0;
            std::optional<std::size_t> fastest;
            for (std::size_t device = 0; device < speeds.size(); ++device)
            {
                total += speeds[device];
                if (speeds[device] > 0.0 && (!fastest || speeds[device] > speeds[*fastest]))
                    fastest = device;
            }
            if (extent == 0 || !fastest)
                return {};

            std::vector<std::uint64_t> shares(speeds.size(), 0);
            std::uint64_t given = 0;
            for (std::size_t device = 0; device < speeds.size(); ++device)
            {
                const double exact = std::floor(static_cast<double>(extent) * speeds[device] / total);
                const std::uint64_t left = extent - given;
                const std::uint64_t share =
                    exact >= static_cast<double>(left) ? left : static_cast<std::uint64_t>(exact);
                shares[device] = share;
                given += share;
            }
            shares[*fastest] += extent - given;

            std::size_t count = 0;
            for (const std::uint64_t share : shares)
                count += share != 0 ? 1 : 0;

            std::vector<SizedPart> parts;
            parts.reserve(count);
            std::uint64_t begin = 0;
            for (std::size_t device = 0; device < shares.size(); ++device)
            {
                if (shares[device] == 0)
                    continue;
                const SplitPart part{axis, begin, begin + shares[device], parts.size(), count};
                parts.push_back(SizedPart{device, part});
                begin += shares[device];
            }
            return parts;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
