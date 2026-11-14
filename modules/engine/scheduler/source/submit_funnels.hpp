/**************************************************************************/
/* submit_funnels.hpp                                                     */
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
 * @file submit_funnels.hpp
 * @brief Declares SubmitFunnels, the submit funnel of each device, or none when the setting is off.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>

#include "submit_funnel.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        /**
         * @brief Owns one SubmitFunnel per device when the funnel is on, and none when it is off.
         * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
         */
        class SubmitFunnels final
        {
            public:
                /**
                 * @brief Builds a funnel per device when @p enabled; starts no thread.
                 * @param outcome,collector Must outlive the funnels.
                 */
                SubmitFunnels(bool enabled, std::size_t devices, std::size_t workers, RunOutcome& outcome,
                              Profile::Collector& collector);

                SubmitFunnels(const SubmitFunnels&) = delete;
                SubmitFunnels& operator=(const SubmitFunnels&) = delete;
                SubmitFunnels(SubmitFunnels&&) = delete;
                SubmitFunnels& operator=(SubmitFunnels&&) = delete;
                ~SubmitFunnels() = default;

                /** @brief Returns whether any device has a funnel. */
                [[nodiscard]] bool is_enabled() const noexcept { return !funnels_.empty(); }

                /** @brief Returns the funnel of device @p device, or null when it has none. */
                [[nodiscard]] SubmitFunnel* of(std::uint32_t device) noexcept;

                /**
                 * @brief Moves every event landed for worker @p worker into @p events.
                 * @return The number of events moved.
                 */
                std::size_t land(std::size_t worker, EventPolling::ActiveEventList& events);

                /** @brief Starts every funnel's thread. */
                void start();

                /** @brief Arms every funnel for a run. */
                void arm();

                /** @brief Marks the run over on every funnel. */
                void disarm() noexcept;

                /**
                 * @brief Stops every funnel's thread.
                 * @pre The workers have ended, so nothing is pushed again and every record has landed.
                 */
                void stop();

                /** @brief Returns the records every funnel has taken since it was built. */
                [[nodiscard]] std::uint64_t funnelled_count() const noexcept;

            private:
                /// @brief One funnel per device, by device index; empty when the setting is off.
                std::vector<std::unique_ptr<SubmitFunnel>> funnels_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
