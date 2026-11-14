/**************************************************************************/
/* submitting_threads.hpp                                                 */
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
 * @file submitting_threads.hpp
 * @brief Declares SubmittingThreads, which collects the distinct threads that submitted a test's device work.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

namespace TestSupport
{
    /**
     * @brief Collects the distinct threads that called a queue's submit for a test's nodes.
     *
     * A command group or a task body calls note() on the thread that submits it; any thread may call it.
     */
    class SubmittingThreads final
    {
        public:
            /** @brief Notes the calling thread. */
            void note()
            {
                const std::thread::id self = std::this_thread::get_id();
                const std::lock_guard<std::mutex> lock(mutex_);
                if (std::find(ids_.begin(), ids_.end(), self) == ids_.end())
                    ids_.push_back(self);
            }

            /** @brief Returns the distinct threads noted, in the order first noted. */
            [[nodiscard]] std::vector<std::thread::id> ids() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return ids_;
            }

        private:
            /// @brief Guards ids_.
            mutable std::mutex mutex_;
            /// @brief The distinct threads, in the order first noted.
            std::vector<std::thread::id> ids_;
    };
} // namespace TestSupport
