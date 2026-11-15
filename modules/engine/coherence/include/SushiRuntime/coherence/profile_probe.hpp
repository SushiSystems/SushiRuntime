/**************************************************************************/
/* profile_probe.hpp                                                    */
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
 * @file profile_probe.hpp
 * @brief Declares ProfileProbe, the port through which ProfileMeasurement times a device.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <string_view>

#include <SushiRuntime/core/operation_kind.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/device_key.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the size of a device's memory pool and the part the runtime must leave free. */
        struct CapacitySample
        {
            std::uint64_t capacity_bytes = 0; /**< Bytes the pool holds. */
            std::uint64_t reserve_bytes = 0;  /**< Bytes of the pool to leave free. */
        };

        /** @brief Holds the bytes one probe copy moved and the seconds it took. */
        struct CopySample
        {
            std::uint64_t bytes = 0; /**< Bytes the copy moved. */
            double seconds = 0.0;    /**< Seconds the copy took. */
        };

        /** @brief Holds the operations one probe kernel did and the seconds it took. */
        struct KernelSample
        {
            std::uint64_t operations = 0; /**< Operations the kernel did, in the unit of its kind. */
            double seconds = 0.0;         /**< Seconds the kernel took. */
        };

        /**
         * @brief Times copies and kernels on a device.
         *
         * The runtime knows no math: a probe copies bytes no consumer reads, and times a kernel the
         * caller registered for each kind. A real probe runs on the device; tests fake it.
         */
        class ProfileProbe
        {
            public:
                virtual ~ProfileProbe() = default;

                /** @brief Returns the pool of device. */
                [[nodiscard]] virtual Result<CapacitySample> measure_capacity(const DeviceKey& device) = 0;

                /** @brief Copies bytes inside the memory of device and returns the time. */
                [[nodiscard]] virtual Result<CopySample> measure_device_copy(const DeviceKey& device,
                                                                             std::uint64_t bytes) = 0;

                /** @brief Copies bytes from source to destination over the path named path_name and returns the time. */
                [[nodiscard]] virtual Result<CopySample> measure_path_copy(const DeviceKey& source,
                                                                           const DeviceKey& destination,
                                                                           std::string_view path_name,
                                                                           std::uint64_t bytes) = 0;

                /** @brief Runs the kernel the caller registered for kind on device and returns the time. */
                [[nodiscard]] virtual Result<KernelSample> measure_kernel(const DeviceKey& device,
                                                                          OperationKind kind) = 0;
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
