/**************************************************************************/
/* device_profile_probe.hpp                                               */
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
 * @file device_profile_probe.hpp
 * @brief Declares DeviceProfileProbe, the ProfileProbe that times copies and kernels on SYCL devices.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/operation_kind.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/device_profile/probe_devices.hpp>
#include <SushiRuntime/device_profile/profile_probe.hpp>
#include <SushiRuntime/transfer/transfer_path.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /**
         * @brief Runs a caller's kernel of one kind on queue to completion and returns the
         *        operations it did, in the unit of its kind.
         */
        using ProbeKernel = std::function<Result<std::uint64_t>(sycl::queue& queue)>;

        /**
         * @brief Times copies and kernels on real devices, through standard SYCL only.
         *
         * Each measurement runs once to warm up and once timed, from submit to the end of the
         * wait. A path copy goes through the runtime's TransferPath of that name. The probe owns
         * no kernel: the caller registers one per kind. The integrated-detection hook is in the
         * module README.
         */
        class DeviceProfileProbe final : public ProfileProbe
        {
            public:
                /**
                 * @brief Builds a probe; devices and the paths must outlive it.
                 * @param paths   The runtime's paths, searched by name.
                 * @param kernels The kernel the caller registered for each kind.
                 */
                DeviceProfileProbe(const ProbeDevices& devices, std::vector<TransferPath*> paths,
                                   std::map<OperationKind, ProbeKernel> kernels);

                [[nodiscard]] Result<CapacitySample> measure_capacity(const DeviceKey& device) override;

                [[nodiscard]] Result<CopySample> measure_device_copy(const DeviceKey& device,
                                                                     std::uint64_t bytes) override;

                [[nodiscard]] Result<CopySample> measure_path_copy(const DeviceKey& source,
                                                                   const DeviceKey& destination,
                                                                   std::string_view path_name,
                                                                   std::uint64_t bytes) override;

                [[nodiscard]] Result<CopySample> measure_host_copy(const DeviceKey& device,
                                                                   HostDirection direction,
                                                                   std::uint64_t bytes) override;

                [[nodiscard]] Result<KernelSample> measure_kernel(const DeviceKey& device,
                                                                  OperationKind kind) override;

            private:
                /** @brief Returns the device with key, or InvalidArgument naming it. */
                [[nodiscard]] Result<ProbeDevice> device_of(const DeviceKey& key) const;

                /// @brief Finds a device by its key.
                const ProbeDevices& devices_;

                /// @brief The runtime's paths, searched by name.
                std::vector<TransferPath*> paths_;

                /// @brief The kernel of each kind.
                std::map<OperationKind, ProbeKernel> kernels_;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
