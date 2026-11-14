/**************************************************************************/
/* submit_funnel.hpp                                                      */
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
 * @file submit_funnel.hpp
 * @brief Declares SubmitFunnel, the one thread that submits a device's dispatches for every worker.
 * @author Mustafa Garip
 * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/profile/profile.hpp>
#include <SushiRuntime/report/node_event_record.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>
#include <SushiRuntime/work_stealing/mpsc_ring.hpp>
#include <SushiRuntime/work_stealing/spsc_ring.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Internal::Scheduler
    {
        class Task;

        /**
         * @brief Holds what a worker knows at the submit site of a dispatch, handed to the funnel.
         * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
         */
        struct FunnelRecord
        {
            Task* task = nullptr;                              /**< The task; the worker's reference travels with it. */
            Internal::Graph::Node* node = nullptr;             /**< The task as a graph node, or null for a plain task. */
            sycl::queue* queue = nullptr;                      /**< The queue select_dispatch_queue() chose. */
            bool profiling = false;                            /**< The run's profiling flag, read once for the dispatch. */
            std::uint32_t step = 0;                            /**< The step the dispatch belongs to. */
            std::uint32_t device_index = K_HOST_DEVICE_INDEX;  /**< The device of the queue. */
            std::size_t worker = 0;                            /**< The worker the event lands back on. */
        };

        /**
         * @brief Owns one device's record ring, a landing ring per worker and the thread that submits between them.
         *
         * The thread submits a kernel node's command group and runs a HOST node's body, so the body's own
         * submissions leave from it too. Workers push and land without a lock; the thread takes a mutex only to
         * park while no run is armed.
         * @see docs/design/GRAPH_REPLAY/SUBMIT_FUNNEL.md
         */
        class SubmitFunnel final
        {
            public:
                /** @brief The records the ring holds; a worker whose push finds it full keeps the record and pushes it again. */
                static constexpr std::size_t K_RECORD_CAPACITY = 1024;

                /** @brief The events one worker's landing ring holds; the rest wait in the thread's own overflow. */
                static constexpr std::size_t K_LANDING_CAPACITY = 256;

                /**
                 * @brief Builds the rings of @p workers workers; starts no thread.
                 * @param device_index The device whose dispatches the funnel submits.
                 * @param outcome Takes the failure of a submission; must outlive the funnel.
                 * @param collector Takes the funnel thread's zones; must outlive the funnel.
                 */
                SubmitFunnel(std::uint32_t device_index, std::size_t workers, RunOutcome& outcome,
                             Profile::Collector& collector);

                SubmitFunnel(const SubmitFunnel&) = delete;
                SubmitFunnel& operator=(const SubmitFunnel&) = delete;
                SubmitFunnel(SubmitFunnel&&) = delete;
                SubmitFunnel& operator=(SubmitFunnel&&) = delete;

                /** @brief Stops and joins the thread. */
                ~SubmitFunnel();

                /** @brief Spawns the submitting thread; a second call does nothing. */
                void start();

                /**
                 * @brief Hands a record to the thread; any worker may call it, and it takes no lock.
                 * @param record Moved from on success, left as it was on failure.
                 * @return False when the ring is full; the record stays with the worker.
                 */
                [[nodiscard]] bool try_push(FunnelRecord& record);

                /**
                 * @brief Takes one landed event of worker @p worker; only that worker may call it.
                 * @return False when nothing has landed for the worker.
                 */
                [[nodiscard]] bool try_land(std::size_t worker, EventPolling::PolledEvent& event);

                /** @brief Marks a run armed and wakes a parked thread; takes the park mutex only on that edge. */
                void arm();

                /** @brief Marks the run over, so the idle thread may park. */
                void disarm() noexcept;

                /**
                 * @brief Asks the thread to end and joins it.
                 * @pre No worker pushes again and every pushed record has landed.
                 */
                void stop();

                /** @brief Returns how many records the thread has taken since it was built. */
                [[nodiscard]] std::uint64_t funnelled_count() const noexcept;

            private:
                /** @brief The ring workers push records into. */
                using RecordRing = MPSCRing<FunnelRecord, K_RECORD_CAPACITY>;

                /** @brief The ring one worker's events land on. */
                using LandingRing = SPSCRing<EventPolling::PolledEvent, K_LANDING_CAPACITY>;

                /** @brief Pops and submits records until stop() is asked and the ring is empty. */
                void run();

                /** @brief Submits one record, or runs its host body, as the worker would have, and lands its event. */
                void submit(FunnelRecord& record);

                /** @brief Lands @p event for @p worker, in the thread's overflow when the ring is full. */
                void land(std::size_t worker, EventPolling::PolledEvent event);

                /** @brief Moves overflowed events into their landing rings while the rings have room. */
                void flush_overflow();

                /** @brief Waits until a run is armed or stop() is asked, or for at most K_PARK_TIMEOUT. */
                void park();

                /// @brief The device whose dispatches the funnel submits.
                std::uint32_t device_index_;
                /// @brief Takes a submission's failure.
                RunOutcome& outcome_;
                /// @brief Takes the dispatch.submit zones of the thread.
                Profile::Collector& collector_;
                /// @brief The records workers pushed.
                RecordRing records_;
                /// @brief One landing ring per worker.
                std::vector<std::unique_ptr<LandingRing>> landings_;
                /// @brief Events whose landing ring was full, per worker; touched by the thread alone.
                std::vector<std::deque<EventPolling::PolledEvent>> overflow_;
                /// @brief The events in overflow_ across workers; touched by the thread alone.
                std::size_t overflowed_ = 0;
                /// @brief Whether a run is armed.
                std::atomic<bool> armed_{false};
                /// @brief Whether stop() was asked.
                std::atomic<bool> stopping_{false};
                /// @brief The records the thread has taken.
                std::atomic<std::uint64_t> funnelled_{0};
                /// @brief Guards the park edge only.
                std::mutex park_mutex_;
                /// @brief Wakes a parked thread.
                std::condition_variable park_;
                /// @brief The submitting thread.
                std::thread thread_;
        };
    } // namespace Internal::Scheduler
} // namespace SushiRuntime
