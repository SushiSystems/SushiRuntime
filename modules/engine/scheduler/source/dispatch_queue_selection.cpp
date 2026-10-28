/**************************************************************************/
/* dispatch_queue_selection.cpp                                           */
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
 * @file dispatch_queue_selection.cpp
 * @brief Chooses the queue and the device a dispatch goes to.
 * @author Mustafa Garip
 */

#include "dispatch_queue_selection.hpp"

#include <cstddef>
#include <optional>

#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        namespace
        {
            /**
             * @brief Picks which of a device's two queues a dispatch uses.
             * @return The profiling queue under profiling, the plain queue otherwise.
             */
            sycl::queue& queue_for(Internal::Device::DeviceQueueSet& devices, std::size_t index,
                                   bool profiling)
            {
                return profiling ? devices.queue(index, Internal::Device::QueueFlavour{false, true})
                                 : devices.queue(index);
            }

            /**
             * @brief Finds the device index behind a plain queue of the set.
             * @return Its device index, or SIZE_MAX if none matches.
             */
            std::size_t device_of(Internal::Device::DeviceQueueSet& devices, const sycl::queue& queue)
            {
                const std::size_t n = devices.count();
                for (std::size_t i = 0; i < n; ++i)
                    if (devices.queue(i) == queue)
                        return i;
                return static_cast<std::size_t>(-1);
            }
        }

        /**
         * @brief Picks the queue of a dispatch: the node's cached device, or a NUMA-local rotation.
         */
        DispatchQueueSelection select_dispatch_queue(Internal::Device::DeviceQueueSet& devices,
                                                     const Internal::Graph::Node* node, bool profiling,
                                                     int fallback_numa)
        {
            DispatchQueueSelection selection;

            // The first dispatch of a node cached its placement (modules/engine/scheduler/README.md).
            if (node)
            {
                const std::size_t index = static_cast<std::size_t>(node->cached_queue_index_);
                selection.device_index = static_cast<std::uint32_t>(node->cached_queue_index_);
                selection.queue = &queue_for(devices, index, profiling);
                return selection;
            }

            sycl::queue& picked = devices.queue_or_numa_local(std::nullopt, fallback_numa);
            selection.queue = &picked;

            // Skips the index lookup entirely on the default (non-profiled) path.
            if (!profiling)
                return selection;

            const std::size_t idx = device_of(devices, picked);
            if (idx != static_cast<std::size_t>(-1))
            {
                selection.device_index = static_cast<std::uint32_t>(idx);
                selection.queue = &queue_for(devices, idx, profiling);
                return selection;
            }

            selection.unmatched = true;
            return selection;
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
