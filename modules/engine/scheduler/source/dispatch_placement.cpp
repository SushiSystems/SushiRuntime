/**************************************************************************/
/* dispatch_placement.cpp                                                 */
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
 * @file dispatch_placement.cpp
 * @brief Places a graph node on a device at its first dispatch.
 * @author Mustafa Garip
 */

#include <SushiRuntime/scheduler/dispatch_placement.hpp>

#include <SushiLog/text/text_format.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        Result<void> place_for_dispatch(Internal::Graph::Node& node, const Internal::Memory::USMAllocator& allocator,
                                        const HandleHomeReader& handles, std::size_t device_count)
        {
            const Internal::Graph::USMPlacementMemory memory(allocator, handles);

            // The placement found at the first dispatch holds for every later step of the node.
            if (node.locality_checked_)
                return {};

            // A node over buffers on several devices runs with the copies the coherence planner added.
            const Result<std::size_t> placed = Internal::Graph::placement_of(node, memory);
            if (!placed)
                return make_error(placed.error());

            const std::size_t placement = placed.value();
            if (placement >= device_count)
                return make_error(Error{
                    ErrorCode::InvalidGraph,
                    SushiLog::format_text(
                        "it is placed on device {}, but the runtime holds {} device(s); "
                        "pass place_on() an index below {}.",
                        placement, device_count, device_count)});

            node.cached_queue_index_ = static_cast<int>(placement);
            node.locality_checked_ = true;
            return {};
        }

    } // namespace Scheduler
} // namespace SushiRuntime
