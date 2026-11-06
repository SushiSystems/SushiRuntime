/**************************************************************************/
/* recording_task.hpp                                                     */
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
 * @file recording_task.hpp
 * @brief Declares RecordingTask, a host Task that counts what the scheduler asks of it and submits nothing.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief A host task that counts each call the scheduler makes and submits no device work.
         *
         * Its name and priority are set at construction. It never splits. execute_host() records
         * how many dependencies it was given and returns a default event, so no queue sees work.
         */
        class RecordingTask final : public Task
        {
            public:
                /**
                 * @brief Builds a task with a name and a priority.
                 * @param task_name A name that outlives the task, such as a string literal.
                 * @param task_priority The priority the scheduler reads.
                 */
                explicit RecordingTask(const char* task_name = "recording_task", int task_priority = 0)
                    : name_(task_name), priority_(task_priority)
                {
                }

                /** @return The name given at construction. */
                const char* name() const noexcept override
                {
                    return name_;
                }

                /** @return ExecutionMode::Host. */
                ExecutionMode execution_mode() const override
                {
                    return ExecutionMode::Host;
                }

                /** @return false: the task submits nothing to a device. */
                bool submits_device_work() const noexcept override
                {
                    return false;
                }

                /** @return The priority given at construction. */
                int priority() const noexcept override
                {
                    return priority_;
                }

                /** @brief Counts one native run. */
                void execute_native() override
                {
                    ++native_runs_;
                }

                /**
                 * @brief Counts one host run and keeps the number of dependencies it was given.
                 * @param queue Not used.
                 * @param dependencies The events the run was to wait on.
                 * @return A default event: nothing was submitted.
                 */
                sycl::event execute_host(sycl::queue& queue, const std::vector<sycl::event>& dependencies) override
                {
                    static_cast<void>(queue);
                    ++host_runs_;
                    last_dependency_count_ = dependencies.size();
                    return {};
                }

                /** @brief Counts one completion. */
                void on_complete(ReadySink& sink) override
                {
                    static_cast<void>(sink);
                    ++completions_;
                }

                /** @return std::nullopt: the task never splits. */
                std::optional<sushi_ptr<Task>> split() override
                {
                    return std::nullopt;
                }

                /** @return false. */
                bool is_splittable() const override
                {
                    return false;
                }

                /** @return How many times execute_native() ran. */
                std::size_t native_runs() const
                {
                    return native_runs_;
                }

                /** @return How many times execute_host() ran. */
                std::size_t host_runs() const
                {
                    return host_runs_;
                }

                /** @return The dependency count of the latest execute_host() call, or 0 before any. */
                std::size_t last_dependency_count() const
                {
                    return last_dependency_count_;
                }

                /** @return How many times on_complete() ran. */
                std::size_t completions() const
                {
                    return completions_;
                }

            private:
                const char* name_;
                int priority_;
                std::size_t native_runs_ = 0;
                std::size_t host_runs_ = 0;
                std::size_t last_dependency_count_ = 0;
                std::size_t completions_ = 0;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
