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
 * @file cluster_monitor.cpp
 * @brief Implements ClusterMonitor, the shell that runs plan_pass() on a thread.
 * @author Mustafa Garip
 * @see modules/extension/cluster_monitor/README.md
 */

#include <SushiRuntime/cluster_monitor/cluster_monitor.hpp>

#include <algorithm>
#include <exception>
#include <utility>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/cluster_monitor/cluster_monitor_log_category.hpp>
#include <SushiRuntime/cluster_monitor/unsent_frame_log.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>
#include <SushiRuntime/offload_coordination/task_resubmission.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        ClusterMonitor::ClusterMonitor(WorkerDirectory& directory, PendingTasks& pending,
                                       HeldNodes& held, BufferRegistry& registry,
                                       BufferExchange& exchange, Transport& transport,
                                       const ClusterMonitorOptions& options, const Clock& clock)
            : directory_(directory),
              pending_(pending),
              held_(held),
              registry_(registry),
              exchange_(exchange),
              transport_(transport),
              clock_(clock),
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

        void ClusterMonitor::wake_by(std::chrono::steady_clock::time_point moment) noexcept
        {
            if (moment.time_since_epoch().count() >= next_pass_.load(std::memory_order_acquire))
                return;

            {
                // The lock orders this notify after the thread waits or after it reads the new end.
                const std::lock_guard<std::mutex> lock(mutex_);
            }
            wake_.notify_all();
        }

        std::chrono::milliseconds ClusterMonitor::liveness_timeout() const noexcept
        {
            return std::chrono::milliseconds(liveness_timeout_ms_.load(std::memory_order_relaxed));
        }

        std::chrono::milliseconds ClusterMonitor::period() const noexcept
        {
            return pass_period(
                options_.period, liveness_timeout(),
                std::chrono::milliseconds(task_deadline_ms_.load(std::memory_order_relaxed)));
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

        std::chrono::steady_clock::time_point ClusterMonitor::now() const noexcept
        {
            return std::chrono::steady_clock::time_point(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::nanoseconds(clock_.now_ns())));
        }

        void ClusterMonitor::run() noexcept
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!stopping_)
            {
                lock.unlock();
                try
                {
                    run_pass(*sink_);
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR(K_CLUSTER_MONITOR_LOG_CATEGORY, "cluster monitor pass failed: {}; the next pass runs in {} ms",
                                 error.what(), period().count());
                }
                catch (...)
                {
                    SR_LOG_ERROR(K_CLUSTER_MONITOR_LOG_CATEGORY, "cluster monitor pass failed: unknown exception; the next pass "
                                 "runs in {} ms",
                                 period().count());
                }
                lock.lock();

                // A setter's notify ends the wait early, so the next pass reads the new period.
                if (!stopping_)
                {
                    const std::chrono::steady_clock::time_point at = now();
                    const std::chrono::steady_clock::time_point next =
                        next_pass_at(at, period(), held_.earliest(), pending_.earliest_deadline());
                    next_pass_.store(next.time_since_epoch().count(), std::memory_order_release);
                    wake_.wait_for(lock, next - at);
                    next_pass_.store(K_IN_A_PASS, std::memory_order_release);
                }
            }
        }

        void ClusterMonitor::run_pass(Internal::Graph::OffloadSink& sink)
        {
            ping_workers();

            const std::chrono::steady_clock::time_point at = now();
            const MonitorActions actions = plan_pass(state_, gather(at), at);

            close_peers(actions);
            resubmit_reaped(actions, sink);
            log_reaped_tasks(actions);
            release_expired_holds(at, sink);
        }

        MonitorFindings ClusterMonitor::gather(std::chrono::steady_clock::time_point at)
        {
            MonitorFindings findings;

            peers_.clear();
            directory_.take_silent(at, liveness_timeout(), peers_);
            findings.silent = peers_;

            peers_.clear();
            directory_.take_handshake_overdue(at, options_.handshake_timeout, peers_);
            findings.handshake_overdue = peers_;

            OverdueLimits limits;
            limits.task_deadline =
                std::chrono::milliseconds(task_deadline_ms_.load(std::memory_order_relaxed));
            limits.acknowledge_timeout = liveness_timeout();

            // tasks_ keeps what a failed call left in it, so the next pass still resubmits it.
            pending_.take_overdue(at, limits, tasks_);
            for (const PendingTask& task : tasks_)
                findings.reaped.push_back(ReapedTask{task.task_id, task.worker, task.cause});
            return findings;
        }

        void ClusterMonitor::ping_workers()
        {
            peers_.clear();
            directory_.list_workers(peers_);

            Wire::PingMessage ping;
            ping.token = clock_.now_ns() / 1000;
            for (const PeerID peer : peers_)
            {
                // A PING that is not queued costs one sample; liveness counts every frame.
                Detail::log_unsent_frame("PING", peer,
                                         send_message(transport_, peer, ping, payload_));
            }
        }

        void ClusterMonitor::close_peers(const MonitorActions& actions)
        {
            Wire::GoodbyeMessage goodbye;
            goodbye.reason = Wire::GoodbyeReason::Liveness;
            goodbye.text = "no frame within the liveness timeout";
            for (const PeerID peer : actions.goodbye_and_close)
            {
                // close_peer() gives a queued control frame time to leave, so the worker reads why.
                Detail::log_unsent_frame("GOODBYE", peer,
                                         send_message(transport_, peer, goodbye, payload_));
                transport_.close_peer(peer);
            }

            for (const PeerID peer : actions.goodbye_and_close)
                SR_LOG_WARN(K_CLUSTER_MONITOR_LOG_CATEGORY, "worker {} sent no frame for more than {} ms; its connection is "
                            "closed and its tasks return to the scheduler",
                            peer.value, liveness_timeout().count());

            for (const PeerID peer : actions.close)
                transport_.close_peer(peer);

            for (const PeerID peer : actions.close)
                SR_LOG_WARN(K_CLUSTER_MONITOR_LOG_CATEGORY, "peer {} sent no valid handshake in {} ms; closed", peer.value,
                            options_.handshake_timeout.count());
        }

        void ClusterMonitor::resubmit_reaped(const MonitorActions& actions,
                                             Internal::Graph::OffloadSink& sink)
        {
            if (tasks_.empty())
                return;

            // The counters rise before the nodes return, so a run that ends on them reads them.
            reaped_deadline_.fetch_add(actions.reaped.deadline, std::memory_order_relaxed);
            reaped_unacknowledged_.fetch_add(actions.reaped.unacknowledged,
                                             std::memory_order_relaxed);
            reaped_lost_result_.fetch_add(actions.reaped.lost_result, std::memory_order_relaxed);

            const std::size_t taken = tasks_.size();
            redispatched_.fetch_add(taken, std::memory_order_relaxed);
            const std::size_t resubmitted = resubmit_tasks(
                SushiRuntime::span<PendingTask>(tasks_.data(), tasks_.size()), registry_,
                exchange_, directory_, sink);
            redispatched_.fetch_sub(taken - resubmitted, std::memory_order_relaxed);
            tasks_.clear();

            if (!actions.reoffer_held)
                return;

            // The reaped tasks freed slots, which a node held for a busy worker may take.
            nodes_.clear();
            held_.take_all(nodes_);
            reoffer_nodes(nodes_, held_, sink);
            nodes_.clear();
        }

        void ClusterMonitor::release_expired_holds(std::chrono::steady_clock::time_point at,
                                                   Internal::Graph::OffloadSink& sink)
        {
            nodes_.clear();
            held_.take_expired(at, nodes_);
            if (nodes_.empty())
                return;

            const std::size_t reoffered = reoffer_nodes(nodes_, held_, sink);
            nodes_.clear();
            SR_LOG_DEBUG(K_CLUSTER_MONITOR_LOG_CATEGORY,
                         "{} held nodes waited out their rule's bound and are offered again",
                         reoffered);
        }

        void ClusterMonitor::log_reaped_tasks(const MonitorActions& actions) const
        {
            for (const ReapWarning& warning : actions.warnings)
                SR_LOG_WARN(K_CLUSTER_MONITOR_LOG_CATEGORY, "task {} taken back from worker {}: {}; it is resubmitted to the "
                            "scheduler",
                            warning.task_id, warning.worker.value,
                            reap_cause_words(warning.cause));
        }

    } // namespace Distributed
} // namespace SushiRuntime
