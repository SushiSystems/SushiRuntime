/**************************************************************************/
/* submit_funnel.cpp                                                      */
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
 * @file submit_funnel.cpp
 * @brief Implements SubmitFunnel: the record ring, the submitting thread and the landing rings.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
 */

#include "submit_funnel.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/scheduler/dispatch_record.hpp>
#include <SushiRuntime/scheduler/failure_naming.hpp>
#include <SushiRuntime/scheduler/scheduler_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Scheduler
    {
        namespace
        {
            /** @brief Idle passes the thread yields through before it may park. */
            constexpr std::size_t K_SPINS_BEFORE_PARK = 100;

            /** @brief The longest a parked thread sleeps before it looks at its ring again. */
            constexpr std::chrono::milliseconds K_PARK_TIMEOUT{100};
        } // namespace

        SubmitFunnel::SubmitFunnel(std::uint32_t device_index, std::size_t workers, RunOutcome& outcome,
                                   Profile::Collector& collector)
            : device_index_(device_index), outcome_(outcome), collector_(collector), overflow_(workers)
        {
            landings_.reserve(workers);
            for (std::size_t i = 0; i < workers; ++i)
                landings_.push_back(std::make_unique<LandingRing>());
        }

        SubmitFunnel::~SubmitFunnel()
        {
            stop();
        }

        void SubmitFunnel::start()
        {
            if (thread_.joinable())
                return;
            thread_ = std::thread([this] { run(); });
        }

        bool SubmitFunnel::try_push(FunnelRecord& record)
        {
            return records_.try_push(record);
        }

        bool SubmitFunnel::try_land(std::size_t worker, EventPolling::PolledEvent& event)
        {
            return landings_[worker]->try_pop(event);
        }

        void SubmitFunnel::arm()
        {
            // An already armed funnel is left alone, so a re-arm from a worker takes no lock.
            if (armed_.exchange(true, std::memory_order_acq_rel))
                return;
            {
                const std::lock_guard<std::mutex> lock(park_mutex_);
            }
            park_.notify_one();
        }

        void SubmitFunnel::disarm() noexcept
        {
            armed_.store(false, std::memory_order_release);
        }

        void SubmitFunnel::stop()
        {
            stopping_.store(true, std::memory_order_release);
            {
                const std::lock_guard<std::mutex> lock(park_mutex_);
            }
            park_.notify_one();
            if (thread_.joinable())
                thread_.join();
        }

        std::uint64_t SubmitFunnel::funnelled_count() const noexcept
        {
            return funnelled_.load(std::memory_order_relaxed);
        }

        void SubmitFunnel::run()
        {
            // Registers the thread with the profiler, so the dispatch.submit zones it opens are recorded.
            Internal::Profile::name_this_thread("sr-funnel-" + std::to_string(device_index_));

            std::size_t idle_passes = 0;
            while (true)
            {
                if (overflowed_ != 0)
                    flush_overflow();

                FunnelRecord record;
                if (records_.try_pop(record))
                {
                    submit(record);
                    idle_passes = 0;
                    continue;
                }

                if (stopping_.load(std::memory_order_acquire))
                    return;

                ++idle_passes;
                const bool busy = armed_.load(std::memory_order_acquire) || overflowed_ != 0;
                if (busy || idle_passes < K_SPINS_BEFORE_PARK)
                    std::this_thread::yield();
                else
                    park();
            }
        }

        void SubmitFunnel::submit(FunnelRecord& record)
        {
            Task& task = *record.task;

            EventPolling::PolledEvent dispatched;
            std::optional<Error> failed;
            Result<void> ran = SYCL::translate_exceptions([&]()
            {
                Result<EventPolling::PolledEvent> submitted =
                    submit_dispatch(*record.queue, task, record.node, record.profiling, record.step,
                                    record.device_index, collector_);
                if (submitted)
                    dispatched = std::move(submitted.value());
                else
                    failed = std::move(submitted.error());
            });
            funnelled_.fetch_add(1, std::memory_order_relaxed);
            if (ran && failed)
                ran = make_error(std::move(*failed));

            if (!ran)
            {
                Error failure = name_failure(task, ran.error());
                SR_LOG_ERROR(K_SCHEDULER_LOG_CATEGORY, "{}; the run is aborted and its remaining tasks are skipped.",
                             failure.message);
                outcome_.record(std::move(failure));
                // The failed task lands as ready so its successors fire, as on the worker's own path.
                land(record.worker, skipped_dispatch_event(&task));
                return;
            }

            land(record.worker, std::move(dispatched));
        }

        void SubmitFunnel::land(std::size_t worker, EventPolling::PolledEvent event)
        {
            std::deque<EventPolling::PolledEvent>& waiting = overflow_[worker];
            if (waiting.empty() && landings_[worker]->try_push(event))
                return;
            waiting.push_back(std::move(event));
            ++overflowed_;
        }

        void SubmitFunnel::flush_overflow()
        {
            for (std::size_t worker = 0; worker < overflow_.size(); ++worker)
            {
                std::deque<EventPolling::PolledEvent>& waiting = overflow_[worker];
                while (!waiting.empty() && landings_[worker]->try_push(waiting.front()))
                {
                    waiting.pop_front();
                    --overflowed_;
                }
            }
        }

        void SubmitFunnel::park()
        {
            std::unique_lock<std::mutex> lock(park_mutex_);
            park_.wait_for(lock, K_PARK_TIMEOUT, [this]()
            {
                return armed_.load(std::memory_order_acquire) || stopping_.load(std::memory_order_acquire);
            });
        }
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
