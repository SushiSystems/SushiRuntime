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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file dispatch_placement.cpp
 * @brief Places a graph node on a device at its first dispatch.
 * @author Mustafa Garip
 */

#include "dispatch_placement.hpp"

#include <optional>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_placement.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        namespace
        {
            /** @brief Returns the device an address is recorded on, or -1 when it has no record. */
            int recorded_device_index(const Internal::Memory::USMAllocator& memory, void* address)
            {
                const std::optional<std::size_t> device = memory.device_of(address);
                return device ? static_cast<int>(*device) : -1;
            }

            /** @brief Tells whether a region of @p regions is recorded off the device @p anchor. */
            bool leaves_device(const std::vector<Internal::Memory::ResourceRegion>& regions, int anchor,
                               const Internal::Memory::USMAllocator& memory)
            {
                for (const Internal::Memory::ResourceRegion& region : regions)
                {
                    if (!region.base)
                        continue;

                    const int device = recorded_device_index(memory, region.base);
                    if (device >= 0 && device != anchor)
                        return true;
                }
                return false;
            }
        } // namespace

        void place_for_dispatch(Internal::Graph::Node& node, const Internal::Memory::USMAllocator& memory,
                                std::size_t device_count)
        {
            void* anchor = nullptr;
            if (!node.io_writes_.empty())
                anchor = node.io_writes_.front().base;
            else if (!node.io_reads_.empty())
                anchor = node.io_reads_.front().base;

            const int anchor_device = recorded_device_index(memory, anchor);
            const bool spans = anchor_device >= 0 &&
                               (leaves_device(node.io_reads_, anchor_device, memory) ||
                                leaves_device(node.io_writes_, anchor_device, memory));

            // A failure is not cached, so it recurs each run (modules/engine/scheduler/README.md).
            if (spans)
                throw SushiException(Error{
                    ErrorCode::InvalidGraph,
                    "its buffers span multiple device contexts; co-locate them on "
                    "one device (cross-device execution is not yet supported)."});

            const std::size_t placement = Internal::Graph::placement_of(node, memory);
            if (placement >= device_count)
                throw SushiException(Error{
                    ErrorCode::InvalidGraph,
                    Detail::sr_format(
                        "it is placed on device {}, but the runtime holds {} device(s); "
                        "pass place_on() an index below {}.",
                        placement, device_count, device_count)});

            // The placement found here holds for every later step of the node.
            node.cached_queue_index_ = static_cast<int>(placement);
            node.locality_checked_ = true;
        }

    } // namespace Scheduler
} // namespace SushiRuntime
