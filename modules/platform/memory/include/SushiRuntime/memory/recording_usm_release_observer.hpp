/**************************************************************************/
/* recording_usm_release_observer.hpp                                     */
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
 * @file recording_usm_release_observer.hpp
 * @brief Declares RecordingUSMReleaseObserver, which keeps the address of each release it hears.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <mutex>
#include <vector>

#include <SushiRuntime/memory/usm_release_observer.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Keeps the address of each release it hears, in arrival order, under a mutex.
         *
         * on_release() may be called from several threads at once, so the addresses are guarded
         * by a mutex, and released() returns a copy taken under that mutex.
         * @see modules/platform/memory/README.md
         */
        class RecordingUSMReleaseObserver final : public USMReleaseObserver
        {
            public:
                /**
                 * @brief Appends @p address to the addresses heard so far.
                 * @param address The address being released.
                 */
                void on_release(void* address) noexcept override
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    released_.push_back(address);
                }

                /**
                 * @brief Returns the addresses heard so far, oldest first.
                 * @return A copy; releases heard after the call are not in it.
                 */
                [[nodiscard]] std::vector<void*> released() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return released_;
                }

            private:
                /** @brief Guards released_. */
                mutable std::mutex mutex_;

                /** @brief The addresses heard, in arrival order. */
                std::vector<void*> released_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
