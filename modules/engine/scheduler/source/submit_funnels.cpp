/**************************************************************************/
/* submit_funnels.cpp                                                     */
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
 * @file submit_funnels.cpp
 * @brief Implements SubmitFunnels.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
 */

#include "submit_funnels.hpp"

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        SubmitFunnels::SubmitFunnels(bool enabled, std::size_t devices, std::size_t workers, RunOutcome& outcome,
                                     Profile::Collector& collector)
        {
            if (!enabled)
                return;
            funnels_.reserve(devices);
            for (std::size_t device = 0; device < devices; ++device)
                funnels_.push_back(std::make_unique<SubmitFunnel>(static_cast<std::uint32_t>(device), workers,
                                                                  outcome, collector));
        }

        SubmitFunnel* SubmitFunnels::of(std::uint32_t device) noexcept
        {
            if (device >= funnels_.size())
                return nullptr;
            return funnels_[device].get();
        }

        std::size_t SubmitFunnels::land(std::size_t worker, EventPolling::ActiveEventList& events)
        {
            std::size_t landed = 0;
            EventPolling::PolledEvent event;
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
            {
                while (funnel->try_land(worker, event))
                {
                    events.push(event);
                    ++landed;
                }
            }
            return landed;
        }

        void SubmitFunnels::start()
        {
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
                funnel->start();
        }

        void SubmitFunnels::arm()
        {
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
                funnel->arm();
        }

        void SubmitFunnels::disarm() noexcept
        {
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
                funnel->disarm();
        }

        void SubmitFunnels::stop()
        {
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
                funnel->stop();
        }

        std::uint64_t SubmitFunnels::funnelled_count() const noexcept
        {
            std::uint64_t total = 0;
            for (const std::unique_ptr<SubmitFunnel>& funnel : funnels_)
                total += funnel->funnelled_count();
            return total;
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
