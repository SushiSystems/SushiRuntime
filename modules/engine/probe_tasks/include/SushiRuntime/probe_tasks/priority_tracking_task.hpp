/**************************************************************************/
/* priority_tracking_task.hpp                                             */
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
 * @file priority_tracking_task.hpp
 * @brief Declares the scheduler task that records its priority into a shared list.
 * @author Mustafa Garip
 */

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/task/task_types.hpp>

namespace SushiRuntime
{
    namespace Test
    {
        /**
         * @brief Task that records its priority into a shared list.
         *
         * Used in scheduler tests to assert that priority ordering is respected.
         */
        class PriorityTrackingTask : public Internal::Scheduler::Task
        {
        public:
            /**
             * @brief Constructs a PriorityTrackingTask.
             * @param p The priority level of this task.
             * @param order Reference to the shared list for recording execution order.
             * @param mtx Mutex protecting the shared execution order list.
             */
            PriorityTrackingTask(int p, std::vector<int>& order, std::mutex& mtx)
                : priority_(p), execution_order_(order), order_mutex_(mtx) {}

            const char* name() const noexcept override    { return "PriorityTask"; }

            int priority() const noexcept override        { return priority_; }
            ExecutionMode execution_mode() const override { return ExecutionMode::Host; }

            sycl::event execute_host(sycl::queue&, const std::vector<sycl::event>&) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard<std::mutex> lock(order_mutex_);
                execution_order_.push_back(priority_);
                return {};
            }

            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override             { return false; }

        private:
            int priority_;
            std::vector<int>& execution_order_;
            std::mutex& order_mutex_;
        };
    } // namespace Test
} // namespace SushiRuntime
