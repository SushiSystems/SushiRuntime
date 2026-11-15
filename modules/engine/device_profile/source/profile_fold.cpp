/**************************************************************************/
/* profile_fold.cpp                                                       */
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
 * @file profile_fold.cpp
 * @brief Implements fold_observations().
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device_profile/profile_fold.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            using SampleKey = std::pair<DeviceKey, OperationKind>;

            /** @brief Returns the operations per second one observation measured on its device's profile, if it says any. */
            [[nodiscard]] std::optional<double> throughput_of(const DispatchObservation& observation,
                                                              const DeviceProfile& profile)
            {
                if (!std::isfinite(observation.seconds) || observation.seconds <= 0.0 || observation.cost.operations == 0)
                    return std::nullopt;
                double compute_seconds = observation.seconds;
                if (profile.memory_bandwidth > 0.0)
                    compute_seconds -= static_cast<double>(observation.cost.moved_bytes()) / profile.memory_bandwidth;
                if (compute_seconds <= 0.0)
                    return std::nullopt;
                return static_cast<double>(observation.cost.operations) / compute_seconds;
            }

            /** @brief Returns the median of samples, the lower one of an even count. */
            [[nodiscard]] double lower_median(std::vector<double>& samples)
            {
                const std::size_t middle = (samples.size() - 1) / 2;
                std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(middle), samples.end());
                return samples[middle];
            }

            /** @brief Returns current moved K_FOLD_SMOOTHING_WEIGHT of the way to median; a current that is not positive gives median. */
            [[nodiscard]] double smoothed(double current, double median)
            {
                if (!(current > 0.0))
                    return median;
                return current + K_FOLD_SMOOTHING_WEIGHT * (median - current);
            }
        } // namespace

        DeviceProfileSet fold_observations(const DeviceProfileSet& profiles,
                                           const std::vector<DispatchObservation>& observations)
        {
            std::map<SampleKey, std::vector<double>> samples;
            for (const DispatchObservation& observation : observations)
            {
                const DeviceProfile* profile = profiles.find(observation.device);
                if (profile == nullptr)
                    continue;
                const std::optional<double> throughput = throughput_of(observation, *profile);
                if (throughput.has_value())
                    samples[SampleKey{observation.device, observation.cost.kind}].push_back(*throughput);
            }

            DeviceProfileSet folded = profiles;
            for (auto& entry : samples)
            {
                DeviceProfile profile = *folded.find(entry.first.first);
                if (entry.second.size() < K_MINIMUM_FOLD_SAMPLES)
                    continue;
                const double median = lower_median(entry.second);
                if (entry.first.second.is_generic())
                    profile.base_throughput = smoothed(profile.base_throughput, median);
                else
                {
                    double& throughput = profile.throughputs[entry.first.second];
                    throughput = smoothed(throughput, median);
                }
                folded.set(entry.first.first, std::move(profile));
            }
            return folded;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
