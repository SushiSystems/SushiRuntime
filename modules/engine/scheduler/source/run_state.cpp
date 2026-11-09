/**************************************************************************/
/* run_state.cpp                                                          */
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
 * @file run_state.cpp
 * @brief Implements RunState.
 * @author Mustafa Garip
 */

#include "run_state.hpp"

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        void RunState::log_run_tail() noexcept
        {
            const std::uint64_t aborted = skipped_aborted.exchange(0, std::memory_order_relaxed);
            const std::uint64_t disabled = skipped_disabled.exchange(0, std::memory_order_relaxed);
            const std::uint64_t stall_drives = poller.take_stall_drives();

            if (aborted != 0 && outcome.is_cancelled())
                SR_LOG_INFO(K_SCHEDULER_LOG_CATEGORY, "The run was cancelled; {} tasks were skipped without running.",
                            aborted);
            else if (aborted != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "The run failed; {} tasks were skipped without running.",
                             aborted);
            if (disabled != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} tasks were skipped because their control was off.",
                             disabled);
            if (stall_drives != 0)
                SR_LOG_DEBUG(K_SCHEDULER_LOG_CATEGORY, "{} polls waited on their oldest event after stalling.",
                             stall_drives);
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
