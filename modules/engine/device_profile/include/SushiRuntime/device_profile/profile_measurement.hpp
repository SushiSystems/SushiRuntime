/**************************************************************************/
/* profile_measurement.hpp                                              */
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
 * @file profile_measurement.hpp
 * @brief Declares ProfileMeasurement, which builds a DeviceProfile from a ProfileProbe.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <SushiRuntime/core/operation_kind.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/device_profile_set.hpp>
#include <SushiRuntime/device_profile/profile_probe.hpp>
#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/handle/transfer_path_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names one path to time: the path and the device it leads to. */
        struct PathTarget
        {
            DeviceKey destination; /**< Device the copies of the probe land on. */
            TransferPathID path;   /**< Identity of the path, as TransferPath::id() reports it; the probe selects by it. */
            std::string path_name; /**< Name of the path, as TransferPath::name() reports it; kept in the profile for its file. */
        };

        /** @brief Holds what ProfileMeasurement measures on each device. */
        struct MeasurementPlan
        {
            std::uint64_t latency_probe_bytes = 1;                 /**< Size of the small copy that gives the latency. */
            std::uint64_t bandwidth_probe_bytes = 64ULL << 20;     /**< Size of the large copy that gives the bandwidth. */
            std::vector<OperationKind> kinds;                      /**< Kinds to time; must include OperationKinds::K_GENERIC, which gives the base throughput. */
            std::vector<PathTarget> paths;                         /**< Paths that leave each device, to time. */
            bool host_links = false;                               /**< Whether to time each device's links to and from host memory. */
        };

        /**
         * @brief Measures a device through a ProfileProbe, following a MeasurementPlan.
         *
         * Memory bandwidth counts the bytes read and written. A path's latency is the time of the
         * small copy and its bandwidth is the large copy's bytes over its time less the latency. A
         * kind's throughput is the operations the probe reports over its seconds. The kinds are those
         * the caller registered; the measurement keeps no list of its own.
         */
        class ProfileMeasurement
        {
            public:
                /** @brief Builds a measurement; probe must outlive it. */
                ProfileMeasurement(ProfileProbe& probe, MeasurementPlan plan);

                /**
                 * @brief Measures one device.
                 * @return InvalidArgument for a plan with no K_GENERIC kind or with a large copy no
                 *         bigger than the small one; InvalidState when the probe reports a time of
                 *         zero or less; the probe's own error otherwise.
                 */
                [[nodiscard]] Result<DeviceProfile> measure(const DeviceKey& device);

                /** @brief Measures each device of devices in order, and returns the profiles; stops at the first failure. */
                [[nodiscard]] Result<DeviceProfileSet> measure_all(const std::vector<DeviceKey>& devices);

            private:
                /**
                 * @brief Returns the link copy measures: the small copy's time is the latency, the
                 *        large copy's bytes over its time less the latency is the bandwidth.
                 * @param what Names the copy in the error of a probe that reported no time.
                 */
                [[nodiscard]] Result<PathProfile> link_of(
                    const std::function<Result<CopySample>(std::uint64_t)>& copy, const char* what) const;

                /// @brief The port that times copies and kernels.
                ProfileProbe& probe_;

                /// @brief What to measure on each device.
                MeasurementPlan plan_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
