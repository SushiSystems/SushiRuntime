/**************************************************************************/
/* profile_measurement.cpp                                              */
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
 * @file profile_measurement.cpp
 * @brief Implements ProfileMeasurement.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/profile_measurement.hpp>

#include <algorithm>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the error of a probe that reported no time. */
            [[nodiscard]] Error no_time(const char* what)
            {
                return Error{ErrorCode::InvalidState, std::string("the probe reported no time for ") + what};
            }
        } // namespace

        ProfileMeasurement::ProfileMeasurement(ProfileProbe& probe, MeasurementPlan plan)
            : probe_(probe), plan_(std::move(plan))
        {

        }

        Result<DeviceProfile> ProfileMeasurement::measure(const DeviceKey& device)
        {
            const bool has_generic = std::any_of(plan_.kinds.begin(), plan_.kinds.end(),
                                                 [](OperationKind kind) { return kind.is_generic(); });
            if (!has_generic)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "a measurement plan needs the generic kind for the base throughput"});
            if (plan_.bandwidth_probe_bytes <= plan_.latency_probe_bytes)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the large probe copy must be bigger than the small one"});

            DeviceProfile profile;

            const Result<CapacitySample> capacity = probe_.measure_capacity(device);
            if (!capacity)
                return make_error(Error{capacity.error()});
            profile.capacity_bytes = capacity.value().capacity_bytes;
            profile.reserve_bytes = capacity.value().reserve_bytes;

            const Result<CopySample> own = probe_.measure_device_copy(device, plan_.bandwidth_probe_bytes);
            if (!own)
                return make_error(Error{own.error()});
            if (own.value().seconds <= 0.0)
                return make_error(no_time("a copy inside the device"));
            profile.memory_bandwidth =
                2.0 * static_cast<double>(own.value().bytes) / own.value().seconds;

            for (const OperationKind kind : plan_.kinds)
            {
                const Result<KernelSample> kernel = probe_.measure_kernel(device, kind);
                if (!kernel)
                    return make_error(Error{kernel.error()});
                if (kernel.value().seconds <= 0.0)
                    return make_error(no_time("a kernel"));
                const double throughput =
                    static_cast<double>(kernel.value().operations) / kernel.value().seconds;
                if (kind.is_generic())
                    profile.base_throughput = throughput;
                else
                    profile.throughputs[kind] = throughput;
            }

            for (const PathTarget& target : plan_.paths)
            {
                const Result<CopySample> small =
                    probe_.measure_path_copy(device, target.destination, target.path_name,
                                             plan_.latency_probe_bytes);
                if (!small)
                    return make_error(Error{small.error()});
                const Result<CopySample> large =
                    probe_.measure_path_copy(device, target.destination, target.path_name,
                                             plan_.bandwidth_probe_bytes);
                if (!large)
                    return make_error(Error{large.error()});
                if (large.value().seconds <= 0.0)
                    return make_error(no_time("a path copy"));

                const double latency = std::max(small.value().seconds, 0.0);
                const double moving = large.value().seconds > latency ? large.value().seconds - latency
                                                                      : large.value().seconds;
                profile.paths[target.path_name] =
                    PathProfile{static_cast<double>(large.value().bytes) / moving, latency};
            }
            return profile;
        }

        Result<DeviceProfileSet> ProfileMeasurement::measure_all(const std::vector<DeviceKey>& devices)
        {
            DeviceProfileSet profiles;
            for (const DeviceKey& device : devices)
            {
                Result<DeviceProfile> measured = measure(device);
                if (!measured)
                    return make_error(Error{measured.error()});
                profiles.set(device, std::move(measured.value()));
            }
            return profiles;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
