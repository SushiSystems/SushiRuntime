/**************************************************************************/
/* split_node_run.cpp                                                     */
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
 * @file split_node_run.cpp
 * @brief Implements run_split_node().
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include "split_node_run.hpp"

#include <cstddef>
#include <string>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/placed_split.hpp>
#include <SushiRuntime/task/split_dispatch.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        Result<void> run_split_node(Internal::Graph::Node& node, Internal::Device::DeviceQueueSet& devices)
        {
            const Internal::Graph::PlacedSplit& placed = *node.placed_split;
            if (node.declared_split == nullptr)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        std::string("node ") + node.name() + " was cut into " + std::to_string(placed.parts.size()) +
                                            " parts but declares no split to run them; declare one with its slice."});

            // Every part is submitted before any is waited on; each depends only on the node's input events.
            const span<const Internal::Graph::SplitDispatchPart> parts(placed.parts.data(), placed.parts.size());
            const std::vector<sycl::event> dependencies = node.sycl_deps.to_vector();
            const std::vector<sycl::event> part_events = Internal::Graph::dispatch_split(
                *node.declared_split, parts, span<const sycl::event>(dependencies.data(), dependencies.size()),
                node.kernel_pointers(), [&devices](std::size_t device) -> sycl::queue& { return devices.queue(device); });

            // The node's one wait: its parts' events, never a whole queue (modules/engine/scheduler/README.md).
            Internal::Graph::join_split(*node.declared_split, parts,
                                        span<const sycl::event>(part_events.data(), part_events.size()));
            return {};
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
