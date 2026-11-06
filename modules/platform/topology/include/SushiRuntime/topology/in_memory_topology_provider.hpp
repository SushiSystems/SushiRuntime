/**************************************************************************/
/* in_memory_topology_provider.hpp                                        */
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
 * @file in_memory_topology_provider.hpp
 * @brief Declares InMemoryTopologyProvider, a TopologyProvider that returns the data it was built with.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#pragma once

#include <optional>
#include <utility>
#include <vector>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/machine/device_filter.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        /**
         * @brief Returns the hardware nodes and core map it was built with, or a fixed error.
         *
         * The DeviceFilter passed to enumerate() is not applied; every call returns the stored nodes.
         * @see modules/platform/topology/README.md
         */
        class InMemoryTopologyProvider final : public TopologyProvider
        {
            public:
                /**
                 * @brief Builds a provider whose enumerate() returns the given nodes.
                 * @param nodes The nodes every enumerate() call returns.
                 * @param core_to_numa The map cpu_core_to_numa() returns; empty when not given.
                 */
                explicit InMemoryTopologyProvider(std::vector<HardwareNode> nodes,
                                                  std::vector<int> core_to_numa = {})
                    : nodes_(std::move(nodes)), core_to_numa_(std::move(core_to_numa))
                {

                }

                /**
                 * @brief Builds a provider whose enumerate() always fails with the given error.
                 * @param error The error every enumerate() call returns.
                 * @return The provider.
                 */
                [[nodiscard]] static InMemoryTopologyProvider failing(Error error)
                {
                    InMemoryTopologyProvider provider{std::vector<HardwareNode>{}};
                    provider.error_ = std::move(error);
                    return provider;
                }

                /**
                 * @brief Returns the stored nodes, or the stored error for a failing provider.
                 * @param config The device filter; not applied.
                 */
                [[nodiscard]] Result<std::vector<HardwareNode>>
                enumerate(const DeviceFilter& config = {}) override
                {
                    static_cast<void>(config);
                    if (error_.has_value())
                        return make_error(*error_);
                    return nodes_;
                }

                /**
                 * @brief Returns the core map the provider was built with.
                 */
                std::vector<int> cpu_core_to_numa() const override
                {
                    return core_to_numa_;
                }

            private:
                std::vector<HardwareNode> nodes_;
                std::vector<int> core_to_numa_;
                std::optional<Error> error_;
        };

    } // namespace Internal::Topology
} // namespace SushiRuntime
