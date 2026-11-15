/**************************************************************************/
/* device_profile.hpp                                                   */
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
 * @file device_profile.hpp
 * @brief Declares PathProfile and DeviceProfile, what one device measured about itself.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include <SushiRuntime/core/operation_kind.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the speed of one transfer path that leaves a device. */
        struct PathProfile
        {
            double bandwidth = 0.0; /**< Bytes per second the path moves. */
            double latency = 0.0;   /**< Seconds a copy takes before its first byte moves. */

            /** @brief Returns whether both fields are equal. */
            [[nodiscard]] bool operator==(const PathProfile& other) const noexcept
            {
                return bandwidth == other.bandwidth && latency == other.latency;
            }
        };

        /**
         * @brief Holds what the runtime measured on one device: its pool, its speeds and its paths.
         *
         * Throughputs are operations per second, in the unit of the kind they belong to. A kind with
         * no entry of its own takes base_throughput.
         */
        struct DeviceProfile
        {
            std::uint64_t capacity_bytes = 0; /**< Bytes the device's memory pool holds. */
            std::uint64_t reserve_bytes = 0;  /**< Bytes of the pool the runtime must leave free. */
            double memory_bandwidth = 0.0;    /**< Bytes per second the device reads and writes in its own memory. */
            double base_throughput = 0.0;     /**< Operations per second of a kind with no entry. */
            std::map<OperationKind, double> throughputs;           /**< Operations per second, per kind. */
            std::map<std::string, PathProfile, std::less<>> paths; /**< Paths that leave the device, by name. */

            /** @brief Returns the throughput of kind, or base_throughput when the kind has no entry. */
            [[nodiscard]] double throughput_for(OperationKind kind) const noexcept
            {
                const auto found = throughputs.find(kind);
                return found == throughputs.end() ? base_throughput : found->second;
            }

            /** @brief Returns the path named name, or nullptr when the device has no such path. */
            [[nodiscard]] const PathProfile* find_path(std::string_view name) const noexcept
            {
                const auto found = paths.find(name);
                return found == paths.end() ? nullptr : &found->second;
            }

            /** @brief Returns whether every field is equal. */
            [[nodiscard]] bool operator==(const DeviceProfile& other) const noexcept
            {
                return capacity_bytes == other.capacity_bytes && reserve_bytes == other.reserve_bytes &&
                       memory_bandwidth == other.memory_bandwidth &&
                       base_throughput == other.base_throughput && throughputs == other.throughputs &&
                       paths == other.paths;
            }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
