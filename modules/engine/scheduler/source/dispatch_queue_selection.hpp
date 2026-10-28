/**************************************************************************/
/* dispatch_queue_selection.hpp                                           */
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
 * @file dispatch_queue_selection.hpp
 * @brief Declares the choice of the queue and the device a dispatch goes to.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstdint>

#include <sycl/sycl.hpp>

#include <SushiRuntime/report/node_event_record.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }
    namespace Internal::Device { class DeviceQueueSet; }

    namespace Internal::Scheduler
    {
        /** @brief The queue a dispatch goes to and the device behind it. */
        struct DispatchQueueSelection
        {
            /** @brief The queue to submit on; never null once selected. */
            sycl::queue* queue = nullptr;

            /** @brief The device of the queue, or K_HOST_DEVICE_INDEX when it is not known. */
            std::uint32_t device_index = K_HOST_DEVICE_INDEX;

            /** @brief True when a profiled dispatch found no device for its queue. */
            bool unmatched = false;
        };

        /**
         * @brief Picks the queue of a dispatch: the node's cached device, or a NUMA-local rotation.
         * @param devices The queue set; its profiling queue is taken when profiling is on.
         * @param node The task as a placed graph node, or nullptr for a plain task.
         * @param profiling The run's profiling flag, read once for the dispatch.
         * @param fallback_numa NUMA node whose devices serve a plain task.
         * @return The selection; see DispatchQueueSelection.
         */
        [[nodiscard]] DispatchQueueSelection select_dispatch_queue(Internal::Device::DeviceQueueSet& devices,
                                                                   const Internal::Graph::Node* node,
                                                                   bool profiling, int fallback_numa);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
