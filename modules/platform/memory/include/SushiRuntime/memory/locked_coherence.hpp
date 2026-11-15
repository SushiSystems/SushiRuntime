/**************************************************************************/
/* locked_coherence.hpp                                                   */
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
 * @file locked_coherence.hpp
 * @brief Declares LockedCoherence, the coherence map of one buffer held under its entry's lock.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

#include <SushiRuntime/memory/coherence_intervals.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Holds the coherence map of one live buffer and the lock that guards it.
         *
         * Every reader and writer of a map goes through one of these, so the planner's snapshot,
         * a planned step's effect, host access and horizon retirement never overlap on a buffer.
         * The lock is released when the object is destroyed. An empty object names no live buffer.
         */
        class LockedCoherence final
        {
            public:
                /** @brief Builds an empty object that names no buffer. */
                LockedCoherence() noexcept = default;

                /**
                 * @brief Takes over lock and the map and run count it guards.
                 * @param lock A lock held on the entry's coherence mutex.
                 * @param map The entry's coherence map.
                 * @param runs The entry's count of runs that hold the buffer.
                 */
                LockedCoherence(std::unique_lock<std::mutex> lock, CoherenceIntervals& map,
                                std::uint32_t& runs) noexcept
                    : lock_(std::move(lock)), map_(&map), runs_(&runs)
                {

                }

                LockedCoherence(const LockedCoherence&) = delete;
                LockedCoherence& operator=(const LockedCoherence&) = delete;

                /** @brief Takes over other's lock; other is left empty. */
                LockedCoherence(LockedCoherence&& other) noexcept
                    : lock_(std::move(other.lock_)), map_(std::exchange(other.map_, nullptr)),
                      runs_(std::exchange(other.runs_, nullptr))
                {

                }

                /** @brief Releases the lock held, takes over other's; other is left empty. */
                LockedCoherence& operator=(LockedCoherence&& other) noexcept
                {
                    if (this != &other)
                    {
                        map_ = std::exchange(other.map_, nullptr);
                        runs_ = std::exchange(other.runs_, nullptr);
                        lock_ = std::move(other.lock_);
                    }
                    return *this;
                }

                /** @brief Releases the lock. */
                ~LockedCoherence() = default;

                /** @brief Returns whether the object names a live buffer. */
                [[nodiscard]] explicit operator bool() const noexcept
                {
                    return map_ != nullptr;
                }

                /** @brief Returns the map. @pre The object names a buffer. */
                [[nodiscard]] CoherenceIntervals* operator->() const noexcept
                {
                    return map_;
                }

                /** @brief Returns the map. @pre The object names a buffer. */
                [[nodiscard]] CoherenceIntervals& operator*() const noexcept
                {
                    return *map_;
                }

                /** @brief Returns whether a run of a graph that holds the buffer is in flight. */
                [[nodiscard]] bool held_by_run() const noexcept
                {
                    return runs_ != nullptr && *runs_ != 0;
                }

                /** @brief Counts one more run that holds the buffer. @pre The object names a buffer. */
                void enter_run() noexcept
                {
                    ++*runs_;
                }

                /** @brief Counts one run fewer that holds the buffer; never below zero. */
                void leave_run() noexcept
                {
                    if (runs_ != nullptr && *runs_ != 0)
                        --*runs_;
                }

                /** @brief Returns whether the object names no buffer. */
                [[nodiscard]] friend bool operator==(const LockedCoherence& locked, std::nullptr_t) noexcept
                {
                    return locked.map_ == nullptr;
                }

                /** @brief Returns whether the object names a buffer. */
                [[nodiscard]] friend bool operator!=(const LockedCoherence& locked, std::nullptr_t) noexcept
                {
                    return locked.map_ != nullptr;
                }

            private:
                /// @brief Lock held on the entry's coherence mutex.
                std::unique_lock<std::mutex> lock_;

                /// @brief The guarded map, or null.
                CoherenceIntervals* map_ = nullptr;

                /// @brief The guarded run count, or null.
                std::uint32_t* runs_ = nullptr;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
