/**************************************************************************/
/* held_tasks.cpp                                                         */
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
 * @file held_tasks.cpp
 * @brief Implements HeldTasks.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#include <SushiRuntime/dispatch_executor/held_tasks.hpp>

#include <algorithm>

namespace SushiRuntime
{
    namespace Distributed
    {
        std::uint64_t HeldTasks::hold(std::uint64_t task_id)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            held_.insert(task_id);
            return generation_;
        }

        void HeldTasks::let_go(std::uint64_t task_id, std::uint64_t generation) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation != generation_)
                return;

            held_.erase(task_id);
            idle_value_ = std::max(idle_value_, task_id + 1);
        }

        void HeldTasks::reset() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            held_.clear();
            idle_value_ = 0;
            ++generation_;
        }

        std::uint64_t HeldTasks::oldest() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return held_.empty() ? idle_value_ : *held_.begin();
        }

    } // namespace Distributed
} // namespace SushiRuntime
