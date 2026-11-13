/**************************************************************************/
/* run_queue.cpp                                                          */
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
 * @file run_queue.cpp
 * @brief Implements RunQueue, the bounded hand-over of observations to the consumer.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/run_queue.hpp>

#include <algorithm>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        namespace
        {
            /** @brief Returns the share of the bound @p observation takes: its events, and at least one. */
            std::size_t weight_of(const RunObservation& observation) noexcept
            {
                return std::max<std::size_t>(observation.events.size(), 1U);
            }
        } // namespace

        RunQueue::RunQueue(std::size_t max_events) noexcept : max_events_(max_events == 0 ? 1 : max_events)
        {
        }

        void RunQueue::push(std::uint32_t runtime, const RunObservation& observation)
        {
            QueuedRun run;
            run.runtime = runtime;
            run.observation = observation;

            const std::size_t incoming = weight_of(run.observation);

            const std::lock_guard<std::mutex> lock(mutex_);
            while (!runs_.empty() && events_ + incoming > max_events_)
            {
                overflow_ += runs_.front().observation.events.size();
                events_ -= weight_of(runs_.front().observation);
                runs_.pop_front();
            }
            events_ += incoming;
            runs_.push_back(std::move(run));
        }

        std::vector<QueuedRun> RunQueue::take()
        {
            std::deque<QueuedRun> taken;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                taken.swap(runs_);
                events_ = 0;
            }
            std::vector<QueuedRun> runs;
            runs.reserve(taken.size());
            for (QueuedRun& run : taken)
                runs.push_back(std::move(run));
            return runs;
        }

        std::size_t RunQueue::discard() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const std::size_t dropped = runs_.size();
            runs_.clear();
            events_ = 0;
            return dropped;
        }

        std::uint64_t RunQueue::take_overflow() noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return std::exchange(overflow_, 0U);
        }

        std::size_t RunQueue::size() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return runs_.size();
        }
    } // namespace Profiling
} // namespace SushiRuntime
