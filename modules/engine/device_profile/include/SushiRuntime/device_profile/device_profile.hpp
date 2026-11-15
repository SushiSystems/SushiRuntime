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
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <SushiRuntime/core/operation_kind.hpp>
#include <SushiRuntime/handle/transfer_path_id.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the speed of one transfer path that leaves a device, or of a host link. */
        struct PathProfile
        {
            double bandwidth = 0.0; /**< Bytes per second the path moves. */
            double latency = 0.0;   /**< Seconds a copy takes before its first byte moves. */
            std::string name{};      /**< Name of the path, kept for the profile file and logs; empty for a host link. */

            /** @brief Returns whether every field is equal. */
            [[nodiscard]] bool operator==(const PathProfile& other) const noexcept
            {
                return bandwidth == other.bandwidth && latency == other.latency && name == other.name;
            }
        };

        /** @brief Names the way a copy crosses between host memory and a device. */
        enum class HostDirection
        {
            ToDevice, /**< From host memory into the device's memory. */
            ToHost    /**< From the device's memory into host memory. */
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
            std::map<TransferPathID, PathProfile> paths;           /**< Paths that leave the device, by identity. */
            std::optional<PathProfile> host_to_device;             /**< Link from host memory into the device; empty when unmeasured. */
            std::optional<PathProfile> device_to_host;             /**< Link from the device into host memory; empty when unmeasured. */

            /** @brief Returns the host link in direction, or nullptr when it was not measured. */
            [[nodiscard]] const PathProfile* host_link(HostDirection direction) const noexcept
            {
                const std::optional<PathProfile>& link =
                    direction == HostDirection::ToDevice ? host_to_device : device_to_host;
                return link ? &*link : nullptr;
            }

            /** @brief Returns the throughput of kind, or base_throughput when the kind has no entry. */
            [[nodiscard]] double throughput_for(OperationKind kind) const noexcept
            {
                const auto found = throughputs.find(kind);
                return found == throughputs.end() ? base_throughput : found->second;
            }

            /** @brief Returns the path with identity id, or nullptr when the device has no such path. */
            [[nodiscard]] const PathProfile* find_path(TransferPathID id) const noexcept
            {
                const auto found = paths.find(id);
                return found == paths.end() ? nullptr : &found->second;
            }

            /**
             * @brief Stores profile as the path called name, under the identity that name maps to.
             *
             * This is the one place a name becomes an identity: the profile file calls it when it is
             * read, and a measurement calls it with the name of the path it timed.
             */
            void set_path(std::string name, PathProfile profile)
            {
                const TransferPathID id = TransferPathID::named(name);
                profile.name = std::move(name);
                paths[id] = std::move(profile);
            }

            /** @brief Returns whether every field is equal. */
            [[nodiscard]] bool operator==(const DeviceProfile& other) const noexcept
            {
                return capacity_bytes == other.capacity_bytes && reserve_bytes == other.reserve_bytes &&
                       memory_bandwidth == other.memory_bandwidth &&
                       base_throughput == other.base_throughput && throughputs == other.throughputs &&
                       paths == other.paths && host_to_device == other.host_to_device &&
                       device_to_host == other.device_to_host;
            }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
