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
         * @brief Tells whether a submission reached a device, so its event is polled.
         * @param host Whether the submitted task runs as a host body.
         * @param profiling The run's profiling flag.
         * @param event The event the submission returned.
         * @param handed_over The events the task body handed over.
         * @return True for a kernel, a handed-over event, or an incomplete or timestamped event.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool submitted_device_work(bool host, bool profiling,
                                                                  const sycl::event& event,
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
         * @param submit_steady_ns Steady reading taken before the submit call began.
         * @return The polled event, holding the handed-over events; under profiling also the widths, thread and submit.
         */
        [[nodiscard]] SUSHIRUNTIME_API EventPolling::PolledEvent
        dispatched_event(const sycl::event& event, bool submitted, Task* task, const Internal::Graph::Node* node,
                         bool profiling, std::uint32_t step, std::uint32_t device_index,
                         std::vector<sycl::event> handed_over, std::uint64_t submit_steady_ns);

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
