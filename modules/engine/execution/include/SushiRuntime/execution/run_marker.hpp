/**************************************************************************/
/* run_marker.hpp                                                        */
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
 * @file run_marker.hpp
 * @brief Declares RunMarker, the count of the runs in progress on one engine.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        /**
         * @brief Counts the scheduled run, the chained runs and the idle change of one engine in one atomic.
         *
         * Bit 0 is the scheduled run, bit 1 the idle change, which is no run, and every chained run adds 4,
         * so nested chained runs each keep their own mark.
         * @see modules/engine/execution/README.md
         */
        class RunMarker
        {
            public:
                /**
                 * @brief Sets the scheduled-run bit and waits out an idle change it found.
                 * @return False, leaving the bit set, when a scheduled run already held it.
                 */
                [[nodiscard]] bool try_begin_scheduled() noexcept
                {
                    const std::uint32_t prior = marker_.fetch_or(K_SCHEDULED_RUN, std::memory_order_acq_rel);
                    if ((prior & K_SCHEDULED_RUN) != 0)
                        return false;
                    wait_for_idle_change(prior);
                    return true;
                }

                /** @brief Clears the scheduled-run bit. */
                void end_scheduled() noexcept
                {
                    marker_.fetch_and(~K_SCHEDULED_RUN, std::memory_order_acq_rel);
                }

                /** @brief Marks one chained run in progress, after any idle change it found. */
                void begin_chained() noexcept
                {
                    wait_for_idle_change(marker_.fetch_add(K_CHAINED_RUN, std::memory_order_acq_rel));
                }

                /** @brief Clears the mark one begin_chained() set. */
                void end_chained() noexcept
                {
                    marker_.fetch_sub(K_CHAINED_RUN, std::memory_order_acq_rel);
                }

                /** @brief Takes the idle-change bit, only from an idle marker; returns whether it did. */
                [[nodiscard]] bool begin_idle_change() noexcept
                {
                    std::uint32_t idle = 0;
                    return marker_.compare_exchange_strong(idle, K_IDLE_CHANGE, std::memory_order_acq_rel,
                                                           std::memory_order_acquire);
                }

                /** @brief Clears the idle-change bit. */
                void end_idle_change() noexcept
                {
                    marker_.fetch_and(~K_IDLE_CHANGE, std::memory_order_acq_rel);
                }

                /** @brief Returns whether a scheduled or a chained run is in progress. */
                [[nodiscard]] bool in_progress() const noexcept
                {
                    // acq_rel writes, acquire load: whoever reads the marker sees what its writer did before.
                    return (marker_.load(std::memory_order_acquire) & ~K_IDLE_CHANGE) != 0;
                }

            private:
                /** @brief Bit a scheduled run holds from submit() to complete(). */
                static constexpr std::uint32_t K_SCHEDULED_RUN = 1u;

                /** @brief Bit an idle change holds; it is not a run. */
                static constexpr std::uint32_t K_IDLE_CHANGE = 2u;

                /** @brief Amount one chained run adds while it lasts. */
                static constexpr std::uint32_t K_CHAINED_RUN = 4u;

                /** @brief Waits for an idle change seen in @p prior to release the marker. */
                void wait_for_idle_change(std::uint32_t prior) const noexcept
                {
                    if ((prior & K_IDLE_CHANGE) == 0)
                        return;
                    // A change holds the bit for one pointer store, so the wait is a few yields.
                    while ((marker_.load(std::memory_order_acquire) & K_IDLE_CHANGE) != 0)
                        std::this_thread::yield();
                }

                /** @brief The count itself. */
                std::atomic<std::uint32_t> marker_{0};
        };
    } // namespace Internal::Execution
} // namespace SushiRuntime
