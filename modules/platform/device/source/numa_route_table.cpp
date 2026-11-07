/**************************************************************************/
/* numa_route_table.cpp                                                   */
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
 * @file numa_route_table.cpp
 * @brief Builds the NUMA routes of a device set and hands out the device of each request.
 * @author Mustafa Garip
 * @see modules/platform/device/README.md
 */

#include <SushiRuntime/device/numa_route_table.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/common.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        namespace
        {
            /** @brief Names the broken precondition of a NUMA lookup on an empty table. */
            constexpr const char* K_EMPTY_TABLE_MESSAGE =
                "The device set holds no device, so no NUMA node resolves to one.";

            /** @brief Returns the cursor of the calling thread, which starts every rotation at zero. */
            RotationCursor& thread_cursor() noexcept
            {
                static thread_local RotationCursor cursor;
                return cursor;
            }
        } // namespace

        NUMARouteTable::NUMARouteTable(const std::vector<int>& device_nodes)
        {
            outside_route_.is_fallback = true;
            outside_route_.devices.reserve(device_nodes.size());

            std::size_t node_count = 0;
            for (std::size_t index = 0; index < device_nodes.size(); ++index)
            {
                outside_route_.devices.push_back(index);
                const std::size_t node = static_cast<std::size_t>(placed_node(device_nodes[index]));
                if (node + 1 > node_count)
                    node_count = node + 1;
            }

            routes_.resize(node_count);
            for (std::size_t index = 0; index < device_nodes.size(); ++index)
                routes_[static_cast<std::size_t>(placed_node(device_nodes[index]))].devices.push_back(index);

            for (Route& route : routes_)
            {
                if (route.devices.empty())
                    route = outside_route_;
            }
        }

        int NUMARouteTable::placed_node(int reported) noexcept
        {
            if (reported < 0 || static_cast<std::size_t>(reported) >= K_MAX_NUMA_NODES)
                return 0;
            return reported;
        }

        const NUMARouteTable::Route& NUMARouteTable::route_for(int numa_node) const noexcept
        {
            if (numa_node < 0 || static_cast<std::size_t>(numa_node) >= routes_.size())
                return outside_route_;
            return routes_[static_cast<std::size_t>(numa_node)];
        }

        std::size_t NUMARouteTable::device_for(int numa_node) const
        {
            const Route& route = route_for(numa_node);
            SR_VERIFY(!route.devices.empty(), K_EMPTY_TABLE_MESSAGE);
            return route.devices.front();
        }

        std::size_t NUMARouteTable::device_or_next(std::optional<std::size_t> device, int fallback_numa) const
        {
            return device_or_next(device, fallback_numa, thread_cursor());
        }

        std::size_t NUMARouteTable::device_or_next(std::optional<std::size_t> device, int fallback_numa,
                                                   RotationCursor& cursor) const
        {
            if (device)
                return *device;

            const Route& route = route_for(fallback_numa);
            SR_VERIFY(!route.devices.empty(), K_EMPTY_TABLE_MESSAGE);
            std::size_t& position = route.is_fallback ? cursor.fallback
                                                      : cursor.numa[static_cast<std::size_t>(fallback_numa)];
            return route.devices[position++ % route.devices.size()];
        }

        std::size_t NUMARouteTable::node_count() const noexcept
        {
            return routes_.size();
        }

        std::size_t NUMARouteTable::populated_count() const noexcept
        {
            std::size_t populated = 0;
            for (const Route& route : routes_)
            {
                if (!route.is_fallback)
                    ++populated;
            }
            return populated;
        }

        std::string NUMARouteTable::empty_nodes() const
        {
            std::string listed;
            for (std::size_t node = 0; node < routes_.size(); ++node)
            {
                if (!routes_[node].is_fallback)
                    continue;
                if (!listed.empty())
                    listed += ", ";
                listed += std::to_string(node);
            }
            return listed;
        }
    } // namespace Internal::Device
} // namespace SushiRuntime
