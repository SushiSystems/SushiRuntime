/**************************************************************************/
/* dispatch_record.hpp                                                    */
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

/**
 * @file dispatch_record.hpp
 * @brief Declares the functions that decide a dispatch's pairing claim and build its polled event.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#pragma once

#include <cstdint>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Tells whether a dispatch may claim its device's clock pairing.
         * @param profiling The run's profiling flag, read once for the dispatch.
         * @param host Whether the dispatched task runs as a host body.
         * @param node The task as a graph node, or nullptr when it is not one.
         * @return True with profiling on, for a kernel or for a host body of a node that is not the sentinel.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool claims_pairing(bool profiling, bool host,
                                                           const Internal::Graph::Node* node);

        /**
         * @brief Tells whether a submission reached a device, which keeps a pairing claim.
         * @param host Whether the submitted task runs as a host body.
         * @param event The event the submission returned.
         * @param handed_over The events the task body handed over.
         * @return True for a kernel, or for a host body that handed over an event or returned one that
         *         had not completed; a body that ran inline returns a complete event and hands none over.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool submitted_device_work(bool host, const sycl::event& event,
                                                                  const std::vector<sycl::event>& handed_over);

        /**
         * @brief Builds the ready event of a host node that ran inline.
         * @return An event the poller reaps at once, as a completed task.
         */
        [[nodiscard]] SUSHIRUNTIME_API EventPolling::PolledEvent completed_host_event(Task* task);

        /**
         * @brief Builds the event a worker pushes to EventPolling after a submission.
         * @param event The submission's event.
         * @param submitted What submitted_device_work() said of the submission; false pushes it as ready.
         * @param node The task as a graph node, or nullptr; its widths are snapshotted under profiling.
         * @param handed_over The events the task body handed over, moved into the result.
         * @return The polled event; under profiling it carries the widths and the submitting thread.
         */
        [[nodiscard]] SUSHIRUNTIME_API EventPolling::PolledEvent
        dispatched_event(const sycl::event& event, bool submitted, Task* task, const Internal::Graph::Node* node,
                         bool profiling, std::uint32_t step, std::uint32_t device_index,
                         std::vector<sycl::event> handed_over);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
