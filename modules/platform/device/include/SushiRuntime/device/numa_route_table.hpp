/**************************************************************************/
/* numa_route_table.hpp                                                   */
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
 * @file numa_route_table.hpp
 * @brief Declares NUMARouteTable, the routing from a NUMA node to the devices that serve it.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        /**
         * @brief Holds one caller's positions in the rotations of a NUMA route table.
         *
         * The caller owns it, so two callers with two cursors never move each other's rotation.
         */
        struct RotationCursor
        {
            std::size_t numa[K_MAX_NUMA_NODES] = {}; /**< Next position on each node that holds a device. */
            std::size_t fallback = 0;                /**< Next position shared by every device-less node. */
        };

        /**
         * @brief Routes a NUMA node to the devices that serve it, built from the node of each device.
         *
         * A node that holds a device is served by its own devices; a node with none, and any id outside
         * the table, by every device in rotation. The table holds values only and touches no device.
         */
        class SUSHIRUNTIME_API NUMARouteTable final
        {
            public:
                /** @brief Lists the devices a NUMA node's requests are served from. */
                struct Route
                {
                    std::vector<std::size_t> devices; /**< Device indices, ascending. */
                    bool is_fallback = false;         /**< True when the node has no device. */
                };

                /** @brief Builds a table that holds no device. */
                NUMARouteTable() = default;

                /**
                 * @brief Builds the routes from the NUMA node of each device.
                 * @param device_nodes Entry i is the node device i reports; placed_node() places it.
                 */
                explicit NUMARouteTable(const std::vector<int>& device_nodes);

                /**
                 * @brief Returns the node a device that reports @p reported sits on in the table.
                 * @return @p reported when it is in [0, K_MAX_NUMA_NODES), and 0 otherwise.
                 */
                [[nodiscard]] static int placed_node(int reported) noexcept;

                /** @brief Returns the route of a NUMA node; any id is accepted. */
                [[nodiscard]] const Route& route_for(int numa_node) const noexcept;

                /**
                 * @brief Returns the lowest-indexed device of a NUMA node, or device 0 when the node has none.
                 * @pre The table holds a device.
                 */
                [[nodiscard]] std::size_t device_for(int numa_node) const;

                /**
                 * @brief Returns @p device when it is given, or the next device of a NUMA node's rotation on
                 *        this thread.
                 * @param device The device to use; nothing selects the rotation.
                 * @param fallback_numa NUMA node the rotation runs over when @p device is empty.
                 * @pre The table holds a device, or @p device is given.
                 */
                [[nodiscard]] std::size_t device_or_next(std::optional<std::size_t> device, int fallback_numa) const;

                /**
                 * @brief Returns @p device when it is given, or the next device of a NUMA node's rotation
                 *        on @p cursor, which it advances.
                 * @param device The device to use; nothing selects the rotation.
                 * @param fallback_numa NUMA node the rotation runs over when @p device is empty.
                 * @param cursor The caller's positions; untouched when @p device is given.
                 * @pre The table holds a device, or @p device is given.
                 */
                [[nodiscard]] std::size_t device_or_next(std::optional<std::size_t> device, int fallback_numa,
                                                         RotationCursor& cursor) const;

                /** @brief Returns the number of nodes from 0 to the highest one that holds a device. */
                [[nodiscard]] std::size_t node_count() const noexcept;

                /** @brief Returns the number of nodes that hold a device. */
                [[nodiscard]] std::size_t populated_count() const noexcept;

                /** @brief Returns the nodes below node_count() that hold no device, comma-separated, or "". */
                [[nodiscard]] std::string empty_nodes() const;

            private:
                /// @brief Route of each NUMA node from 0 to the highest one that holds a device.
                std::vector<Route> routes_;

                /// @brief Route of every NUMA id outside routes_: all devices.
                Route outside_route_;
        };
    } // namespace Internal::Device
} // namespace SushiRuntime
