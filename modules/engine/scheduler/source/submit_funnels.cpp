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
            workers_.reserve(workers);
            for (std::size_t worker = 0; worker < workers; ++worker)
                workers_.push_back(std::make_unique<WorkerSide>());
        }

        SubmitFunnel* SubmitFunnels::of(std::uint32_t device) noexcept
        {
            if (device >= funnels_.size())
                return nullptr;
            return funnels_[device].get();
        }

        bool SubmitFunnels::hand(const FunnelRecord& record)
        {
            if (record.worker >= workers_.size())
                return false;
            SubmitFunnel* funnel = of(record.device_index);
            if (funnel == nullptr)
                return false;

            // Counted before the push, so the worker's landing drain never sees the count below its events.
            WorkerSide& side = *workers_[record.worker];
            side.waiting.fetch_add(1, std::memory_order_relaxed);
            FunnelRecord pushed = record;
            if (!funnel->try_push(pushed))
                side.backlog.push_back(record);
            return true;
        }

        std::size_t SubmitFunnels::land(std::size_t worker, EventPolling::ActiveEventList& events)
        {
            if (worker >= workers_.size())
                return 0;
            WorkerSide& side = *workers_[worker];
            if (side.waiting.load(std::memory_order_relaxed) == 0)
                return 0;
            if (!side.backlog.empty())
                push_backlog(side);

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
            side.waiting.fetch_sub(landed, std::memory_order_relaxed);
            return landed;
        }

        bool SubmitFunnels::is_idle(std::size_t worker) const noexcept
        {
            if (worker >= workers_.size())
                return true;
            return workers_[worker]->waiting.load(std::memory_order_relaxed) == 0;
        }

        void SubmitFunnels::push_backlog(WorkerSide& side)
        {
            std::size_t kept = 0;
            for (FunnelRecord& record : side.backlog)
            {
                // hand() keeps only records whose device has a funnel, so of() finds one.
                if (!of(record.device_index)->try_push(record))
                    side.backlog[kept++] = record;
            }
            side.backlog.resize(kept);
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
