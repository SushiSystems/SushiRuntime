/**************************************************************************/
/* task_submission.cpp                                                    */
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
 * @file task_submission.cpp
 * @brief Submits one task to a queue behind its event dependencies, with or without an event.
 * @author Mustafa Garip
 */

#include <SushiRuntime/task/task_submission.hpp>

#include <utility>
#include <vector>
#include <sycl/sycl.hpp>
#if __has_include(<sycl/ext/oneapi/experimental/enqueue_functions.hpp>)
#include <sycl/ext/oneapi/experimental/enqueue_functions.hpp>
#define SR_HAS_ENQUEUE_FUNCTIONS 1
#endif
#include <SushiRuntime/task/dispatch_events.hpp>
#include <SushiRuntime/task/small_event_vector.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /**
             * @brief Copies @p dependencies, then @p after, into the vector execute_host() takes.
             * @param dependencies The events, or null for none.
             * @param after One more event, or null for none.
             * @return The events in order.
             */
            std::vector<sycl::event> host_dependencies(const SmallEventVector* dependencies,
                                                       const sycl::event* after = nullptr)
            {
                std::vector<sycl::event> copied;
                copied.reserve((dependencies != nullptr ? dependencies->size() : 0) + (after != nullptr ? 1 : 0));
                if (dependencies != nullptr)
                    for (const sycl::event& event : *dependencies)
                        copied.push_back(event);
                if (after != nullptr)
                    copied.push_back(*after);
                return copied;
            }

            /**
             * @brief Builds the command group that runs @p task after @p dependencies and @p after.
             *
             * One depends_on() per event builds no heap vector.
             * @param dependencies,after The events, or null for none.
             */
            auto kernel_group(Scheduler::Task& task, const SmallEventVector* dependencies,
                              const sycl::event* after = nullptr)
            {
                return [&task, dependencies, after](sycl::handler& h)
                {
                    if (dependencies != nullptr)
                        for (const sycl::event& event : *dependencies)
                            h.depends_on(event);
                    if (after != nullptr)
                        h.depends_on(*after);
                    task.execute(h);
                };
            }
        } // namespace

        /**
         * @brief Submits @p task to @p queue after @p dependencies and returns its event.
         */
        Result<sycl::event> submit_task(sycl::queue& queue, Scheduler::Task& task,
                                        const SmallEventVector* dependencies,
                                        std::vector<sycl::event>* handed_over, const sycl::event* after)
        {
            if (task.execution_mode() != Scheduler::Task::ExecutionMode::Host)
                return queue.submit(kernel_group(task, dependencies, after));

            const std::vector<sycl::event> copied = host_dependencies(dependencies, after);
            if (handed_over == nullptr)
                return task.execute_host_checked(queue, copied);
            // A body that submits several kernels hands each event over (task/dispatch_events.hpp).
            DispatchEvents::DispatchEventScope handing(*handed_over);
            return task.execute_host_checked(queue, copied);
        }

        /**
         * @brief Submits @p task to @p queue after @p dependencies and keeps no event for it.
         */
        Result<void> submit_task_discarding(sycl::queue& queue, Scheduler::Task& task,
                                            const SmallEventVector* dependencies)
        {
            if (task.execution_mode() == Scheduler::Task::ExecutionMode::Host)
            {
                Result<sycl::event> ran = task.execute_host_checked(queue, host_dependencies(dependencies));
                if (!ran)
                    return make_error(std::move(ran.error()));
                return {};
            }
#if defined(SR_HAS_ENQUEUE_FUNCTIONS)
            sycl::ext::oneapi::experimental::submit(queue, kernel_group(task, dependencies));
#else
            static_cast<void>(queue.submit(kernel_group(task, dependencies)));
#endif
            return {};
        }

    } // namespace Internal::Graph
} // namespace SushiRuntime
