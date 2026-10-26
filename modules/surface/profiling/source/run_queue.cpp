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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file run_queue.cpp
 * @brief Implements RunQueue, the bounded hand-over of observations to the consumer.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/run_queue.hpp>

#include <utility>

namespace SushiRuntime
{
    namespace Internal::Profiling
    {
        RunQueue::RunQueue(std::size_t capacity) noexcept : capacity_(capacity == 0 ? 1 : capacity)
        {
        }

        void RunQueue::push(std::uint32_t runtime, const RunObservation& observation)
        {
            QueuedRun run;
            run.runtime = runtime;
            run.observation = observation;

            const std::lock_guard<std::mutex> lock(mutex_);
            while (runs_.size() >= capacity_)
            {
                overflow_ += runs_.front().observation.events.size();
                runs_.pop_front();
            }
            runs_.push_back(std::move(run));
        }

        std::vector<QueuedRun> RunQueue::take()
        {
            std::deque<QueuedRun> taken;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                taken.swap(runs_);
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
