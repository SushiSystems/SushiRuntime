/**************************************************************************/
/* dispatch_record.cpp                                                    */
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
 * @file dispatch_record.cpp
 * @brief Implements the pairing-claim policy of a dispatch and the build of its polled event.
 * @author Mustafa Garip
 * @see modules/engine/scheduler/README.md
 */

#include <utility>

#include <SushiRuntime/observability/steady_time.hpp>
#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/task/task_submission.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        namespace
        {
            /**
             * @brief Tells whether @p event carries profiling timestamps, which a default event never does.
             * @param event A complete event.
             * @return True when its command_end can be read.
             */
            bool has_profiling_info(const sycl::event& event)
            {
                try
                {
                    static_cast<void>(event.get_profiling_info<sycl::info::event_profiling::command_end>());
                    return true;
                }
                catch (const sycl::exception&)
                {
                    return false;
                }
            }
        } // namespace

        /**
         * @brief Tells whether a submission reached a device, so its event is polled.
         */
        bool submitted_device_work(bool host, bool profiling, const sycl::event& event,
                                   const std::vector<sycl::event>& handed_over)
        {
            if (!host || !handed_over.empty())
                return true;
            // A default event compares unequal to another under DPC++, so its complete status identifies it.
            if (event.get_info<sycl::info::event::command_execution_status>()
                != sycl::info::event_command_status::complete)
                return true;
            // A real event that completed before this read still has timestamps under profiling.
            return profiling && has_profiling_info(event);
        }

        /**
         * @brief Builds the ready event of a host node that ran inline.
         */
        EventPolling::PolledEvent completed_host_event(Task* task)
        {
            EventPolling::PolledEvent done;
            done.task = task;
            done.ready = true;
            return done;
        }

        /**
         * @brief Builds the ready event of a task whose work did not run to its end.
         */
        EventPolling::PolledEvent skipped_dispatch_event(Task* task)
        {
            EventPolling::PolledEvent skipped;
            skipped.task = task;
            skipped.ready = true;
            skipped.executed = false;
            return skipped;
        }

        /**
         * @brief Builds the event a worker pushes to EventPolling after a submission.
         */
        EventPolling::PolledEvent dispatched_event(const sycl::event& event, bool submitted, Task* task,
                                                   const Internal::Graph::Node* node, bool profiling,
                                                   std::uint32_t step, std::uint32_t device_index,
                                                   std::vector<sycl::event> handed_over,
                                                   std::uint64_t submit_steady_ns)
        {
            const bool ready = !submitted;
            EventPolling::PolledEvent dispatched;
            dispatched.event = event;
            dispatched.task = task;
            dispatched.ready = ready;
            dispatched.has_event = !ready;
            dispatched.step = step;
            if (node && profiling)
                dispatched.widths = Internal::Graph::snapshot_widths(*node);
            dispatched.handed_over = std::move(handed_over);
            dispatched.device_index = device_index;
            if (profiling)
            {
                dispatched.submit_thread = Internal::Profile::current_thread_index();
                dispatched.submit_steady_ns = submit_steady_ns;
            }
            return dispatched;
        }

        /**
         * @brief Submits a task, or runs its host body, and builds the event its dispatch polls on.
         */
        Result<EventPolling::PolledEvent> submit_dispatch(sycl::queue& queue, Task& task,
                                                          Internal::Graph::Node* node, bool profiling,
                                                          std::uint32_t step, std::uint32_t device_index,
                                                          Profile::Collector& collector)
        {
            const bool host = task.execution_mode() == Task::ExecutionMode::Host;

            // The kernel cannot start before this reading, which bounds the device's clock pairing.
            const std::uint64_t submit_steady_ns = profiling ? Internal::Observability::steady_now_ns() : 0;

            // Handed-over events are collected only for the ledger (modules/engine/scheduler/README.md).
            Result<sycl::event> submission = sycl::event{};
            std::vector<sycl::event> handed_over;
            {
                SR_ZONE_FINE(collector, "dispatch.submit");
                submission = Internal::Graph::submit_task(queue, task, node ? &node->sycl_deps : nullptr,
                                                          (profiling && host) ? &handed_over : nullptr);
            }
            if (!submission)
                return make_error(std::move(submission.error()));
            const sycl::event& event = submission.value();
            const bool submitted = submitted_device_work(host, profiling, event, handed_over);

            // A submission that reached no device is pushed as ready, never dropped (modules/engine/scheduler/README.md).
            return dispatched_event(event, submitted, &task, node, profiling, step, device_index,
                                    std::move(handed_over), submit_steady_ns);
        }

    } // namespace Internal::Scheduler
} // namespace SushiRuntime
