/**************************************************************************/
/* cluster_monitor.cpp                                                    */
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
 * @file cluster_monitor.cpp
 * @brief Implements ClusterMonitor, the thread that pings, closes silent peers and reaps tasks.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "cluster_monitor.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include "ping_clock.hpp"
#include "task_resubmission.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the passes that fit into the tightest timeout the monitor applies. */
            constexpr std::int64_t K_PASSES_PER_TIMEOUT = 4;

            /** @brief Holds the shortest time between two passes, in milliseconds. */
            constexpr std::int64_t K_MINIMUM_PERIOD_MS = 1;

            /** @brief Holds the time in which one worker gets at most one reap warning. */
            constexpr std::chrono::seconds K_REAP_WARNING_INTERVAL{1};

            /** @brief Holds the words a log line uses for each ReapCause, by its value. */
            constexpr const char* K_REAP_CAUSE_WORDS[] = {
                "no result within the task deadline",
                "the worker did not acknowledge the dispatch",
                "the worker's heartbeats stopped listing it",
            };
        } // namespace

        ClusterMonitor::ClusterMonitor(WorkerDirectory& directory, PendingTasks& pending,
                                       BufferRegistry& registry, BufferExchange& exchange,
                                       Transport& transport, const ClusterMonitorOptions& options)
            : directory_(directory),
              pending_(pending),
              registry_(registry),
              exchange_(exchange),
              transport_(transport),
              options_(options),
              liveness_timeout_ms_(options.liveness_timeout.count()),
              task_deadline_ms_(options.task_deadline.count())
        {

        }

        ClusterMonitor::~ClusterMonitor()
        {
            stop();
        }

        void ClusterMonitor::start(Internal::Graph::OffloadSink& sink)
        {
            sink_ = &sink;
            thread_ = std::thread([this] { run(); });
        }

        void ClusterMonitor::stop() noexcept
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }

            wake_.notify_all();
            if (thread_.joinable())
                thread_.join();
        }

        void ClusterMonitor::set_liveness_timeout(std::chrono::milliseconds timeout) noexcept
        {
            liveness_timeout_ms_.store(timeout.count(), std::memory_order_relaxed);
            wake_.notify_all();
        }

        void ClusterMonitor::set_task_deadline(std::chrono::milliseconds deadline) noexcept
        {
            task_deadline_ms_.store(deadline.count(), std::memory_order_relaxed);
            wake_.notify_all();
        }

        std::chrono::milliseconds ClusterMonitor::liveness_timeout() const noexcept
        {
            return std::chrono::milliseconds(liveness_timeout_ms_.load(std::memory_order_relaxed));
        }

        std::chrono::milliseconds ClusterMonitor::period() const noexcept
        {
            std::int64_t tightest = liveness_timeout_ms_.load(std::memory_order_relaxed);
            const std::int64_t deadline = task_deadline_ms_.load(std::memory_order_relaxed);
            if (deadline > 0 && deadline < tightest)
                tightest = deadline;

            const std::int64_t wanted =
                std::min<std::int64_t>(options_.period.count(), tightest / K_PASSES_PER_TIMEOUT);
            return std::chrono::milliseconds(std::max(wanted, K_MINIMUM_PERIOD_MS));
        }

        ClusterMonitorStatistics ClusterMonitor::statistics() const noexcept
        {
            ClusterMonitorStatistics snapshot;
            snapshot.tasks_reaped_deadline = reaped_deadline_.load(std::memory_order_relaxed);
            snapshot.tasks_reaped_unacknowledged =
                reaped_unacknowledged_.load(std::memory_order_relaxed);
            snapshot.tasks_reaped_lost_result =
                reaped_lost_result_.load(std::memory_order_relaxed);
            snapshot.redispatched = redispatched_.load(std::memory_order_relaxed);
            return snapshot;
        }

        void ClusterMonitor::run() noexcept
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stopping_)
            {
                lock.unlock();
                try
                {
                    run_pass();
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR("cluster monitor pass failed: {}; the next pass runs in {} ms",
                                 error.what(), period().count());
                }
                catch (...)
                {
                    SR_LOG_ERROR("cluster monitor pass failed: unknown exception; the next pass "
                                 "runs in {} ms",
                                 period().count());
                }
                lock.lock();

                // A setter's notify ends the wait early, so the next pass reads the new period.
                if (!stopping_)
                    wake_.wait_for(lock, period());
            }
        }

        void ClusterMonitor::run_pass()
        {
            ping_workers();

            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            close_silent_workers(now);
            close_handshake_overdue(now);
            reap_overdue_tasks(now);
        }

        void ClusterMonitor::ping_workers()
        {
            peers_.clear();
            directory_.list_workers(peers_);

            Wire::PingMessage ping;
            ping.token = make_ping_token();
            for (const PeerID peer : peers_)
            {
                // A PING that is not queued costs one sample; liveness counts every frame.
                const SendStatus sent = send_message(transport_, peer, ping, payload_);
                static_cast<void>(sent);
            }
        }

        void ClusterMonitor::close_silent_workers(std::chrono::steady_clock::time_point now)
        {
            const std::chrono::milliseconds limit = liveness_timeout();
            peers_.clear();
            directory_.take_silent(now, limit, peers_);

            Wire::GoodbyeMessage goodbye;
            goodbye.reason = Wire::GoodbyeReason::Liveness;
            goodbye.text = "no frame within the liveness timeout";
            for (const PeerID peer : peers_)
            {
                // close_peer() gives a queued control frame time to leave, so the worker reads why.
                const SendStatus sent = send_message(transport_, peer, goodbye, payload_);
                static_cast<void>(sent);
                transport_.close_peer(peer);
            }

            for (const PeerID peer : peers_)
                SR_LOG_WARN("worker {} sent no frame for more than {} ms; its connection is "
                            "closed and its tasks return to the scheduler",
                            peer.value, limit.count());
        }

        void ClusterMonitor::close_handshake_overdue(std::chrono::steady_clock::time_point now)
        {
            peers_.clear();
            directory_.take_handshake_overdue(now, options_.handshake_timeout, peers_);

            for (const PeerID peer : peers_)
                transport_.close_peer(peer);

            for (const PeerID peer : peers_)
                SR_LOG_WARN("peer {} sent no valid handshake in {} ms; closed", peer.value,
                            options_.handshake_timeout.count());
        }

        void ClusterMonitor::reap_overdue_tasks(std::chrono::steady_clock::time_point now)
        {
            OverdueLimits limits;
            limits.task_deadline =
                std::chrono::milliseconds(task_deadline_ms_.load(std::memory_order_relaxed));
            limits.acknowledge_timeout = liveness_timeout();

            // tasks_ keeps what a failed call left in it, so the next pass still resubmits it.
            pending_.take_overdue(now, limits, tasks_);
            if (tasks_.empty())
                return;

            // The counters rise before the nodes return, so a run that ends on them reads them.
            std::atomic<std::uint64_t>* const counters[] = {
                &reaped_deadline_, &reaped_unacknowledged_, &reaped_lost_result_};
            for (const PendingTask& task : tasks_)
                counters[static_cast<std::size_t>(task.cause)]->fetch_add(
                    1, std::memory_order_relaxed);

            const std::size_t taken = tasks_.size();
            redispatched_.fetch_add(taken, std::memory_order_relaxed);
            const std::size_t resubmitted = resubmit_tasks(
                SushiRuntime::span<PendingTask>(tasks_.data(), tasks_.size()), registry_,
                exchange_, directory_, *sink_);
            redispatched_.fetch_sub(taken - resubmitted, std::memory_order_relaxed);

            std::vector<PendingTask> reaped;
            reaped.swap(tasks_);

            log_reaped_tasks(reaped, now);

            reaped.clear();
            tasks_.swap(reaped);
        }

        void ClusterMonitor::log_reaped_tasks(const std::vector<PendingTask>& reaped,
                                              std::chrono::steady_clock::time_point now)
        {
            for (auto entry = last_reap_warning_.begin(); entry != last_reap_warning_.end();)
            {
                if (now - entry->second >= K_REAP_WARNING_INTERVAL)
                    entry = last_reap_warning_.erase(entry);
                else
                    ++entry;
            }

            for (const PendingTask& task : reaped)
            {
                if (!last_reap_warning_.try_emplace(task.worker.value, now).second)
                    continue;

                SR_LOG_WARN("task {} taken back from worker {}: {}; it is resubmitted to the "
                            "scheduler",
                            task.task_id, task.worker.value,
                            K_REAP_CAUSE_WORDS[static_cast<std::size_t>(task.cause)]);
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
