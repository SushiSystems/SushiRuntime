/**************************************************************************/
/* cost_model.hpp                                                       */
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
 * @file cost_model.hpp
 * @brief Declares CostModel, which turns a NodeCost or a copy and the device profiles into seconds.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/device_profile/device_profile_set.hpp>
#include <SushiRuntime/handle/device_key.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Prices work and copies in seconds from the profiles it was built with.
         *
         * The profiles are frozen, so equal arguments give equal times in every run. A node takes its
         * operations over the throughput for its kind plus its bytes over the memory bandwidth. A copy
         * takes the path's latency plus its bytes over the path's bandwidth. A rate of zero under
         * work gives an infinite time.
         */
        class CostModel
        {
            public:
                /** @brief Builds a model over profiles. */
                explicit CostModel(DeviceProfileSet profiles) : profiles_(std::move(profiles))
                {

                }

                /** @brief Returns whether device has a profile; a device with none receives no work. */
                [[nodiscard]] bool has_profile(const DeviceKey& device) const
                {
                    return profiles_.has_profile(device);
                }

                /** @brief Returns the profile of device, or nullptr when it has none. */
                [[nodiscard]] const DeviceProfile* profile(const DeviceKey& device) const
                {
                    return profiles_.find(device);
                }

                /** @brief Returns the profiles the model was built with. */
                [[nodiscard]] const DeviceProfileSet& profiles() const noexcept
                {
                    return profiles_;
                }

                /**
                 * @brief Returns the seconds one dispatch of cost takes on device.
                 * @return Nothing when device has no profile.
                 */
                [[nodiscard]] std::optional<double> node_time(const DeviceKey& device,
                                                              const NodeCost& cost) const;

                /**
                 * @brief Returns the seconds a copy of bytes takes over the path with identity path.
                 *
                 * The path is looked up on the source device's profile first, then on the
                 * destination's.
                 * @return Nothing when neither device has a profile that names the path.
                 */
                [[nodiscard]] std::optional<double> transfer_time(const DeviceKey& source,
                                                                  const DeviceKey& destination,
                                                                  TransferPathID path,
                                                                  std::size_t bytes) const;

                /**
                 * @brief Returns the seconds a copy of bytes takes over device's host link in direction.
                 * @return Nothing when device has no profile or never measured that link.
                 */
                [[nodiscard]] std::optional<double> host_copy_time(const DeviceKey& device,
                                                                   HostDirection direction,
                                                                   std::size_t bytes) const;

                /**
                 * @brief Returns the seconds a copy of bytes takes from source to destination through host memory.
                 *
                 * It is the source's device-to-host link plus the destination's host-to-device link.
                 * @return Nothing when either link was not measured.
                 */
                [[nodiscard]] std::optional<double> host_staged_time(const DeviceKey& source,
                                                                     const DeviceKey& destination,
                                                                     std::size_t bytes) const;

            private:
                /// @brief The frozen profiles.
                DeviceProfileSet profiles_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
