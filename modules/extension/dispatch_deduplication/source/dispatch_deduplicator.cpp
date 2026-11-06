/**************************************************************************/
/* dispatch_deduplicator.cpp                                              */
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
 * @file dispatch_deduplicator.cpp
 * @brief Implements DispatchDeduplicator, a bounded memory of dispatched task ids.
 * @author Mustafa Garip
 */

#include <SushiRuntime/dispatch_deduplication/dispatch_deduplicator.hpp>

#include <algorithm>

namespace SushiRuntime
{
    namespace Distributed
    {
        DispatchDeduplicator::DispatchDeduplicator(std::size_t capacity)
            : capacity_(std::max<std::size_t>(capacity, 1))
        {
            seen_.reserve(capacity_);
        }

        bool DispatchDeduplicator::mark_seen(std::uint64_t task_id)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!seen_.insert(task_id).second)
                return false;

            order_.push_back(task_id);
            if (order_.size() > capacity_)
            {
                seen_.erase(order_.front());
                order_.pop_front();
            }
            return true;
        }

        void DispatchDeduplicator::clear() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            seen_.clear();
            order_.clear();
        }

        std::size_t DispatchDeduplicator::size() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return order_.size();
        }

    } // namespace Distributed
} // namespace SushiRuntime
