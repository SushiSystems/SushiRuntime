/**************************************************************************/
/* cluster_monitor.hpp                                                    */
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
 * @file cluster_monitor.hpp
 * @brief Declares ClusterMonitor, the master's thread for pings, liveness and deadlines.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

#include "buffer_exchange.hpp"
#include "buffer_registry.hpp"
#include "pending_tasks.hpp"
#include "worker_directory.hpp"

namespace SushiRuntime
{
    namespace Graph { class OffloadSink; }

    namespace Distributed
    {
        /**
         * @brief Holds the timings the monitor starts with.
         */
        struct ClusterMonitorOptions
        {
            /** @brief Holds the time between two passes, which is also the ping interval. */
            std::chrono::milliseconds period{250};

            /** @brief Holds the silence after which a worker is closed. */
            std::chrono::milliseconds liveness_timeout{5000};

            /** @brief Bounds a kernel's run on a worker; 0 sets no bound. */
            std::chrono::milliseconds task_deadline{30000};

            /** @brief Bounds the time a connection has to finish the handshake. */
            std::chrono::milliseconds handshake_timeout{5000};
        };

        /**
         * @brief Holds the monitor's counters since it was built.
         */
        struct ClusterMonitorStatistics
        {
            std::uint64_t tasks_reaped_deadline = 0;       /**< Tasks past the task deadline. */
            std::uint64_t tasks_reaped_unacknowledged = 0; /**< Dispatches nobody confirmed. */
            std::uint64_t tasks_reaped_lost_result = 0;    /**< Tasks a worker stopped holding. */
            std::uint64_t redispatched = 0;                /**< Tasks the monitor resubmitted. */
        };

        /**
         * @brief Runs the thread that pings workers, closes silent peers and reaps tasks.
         *
         * A pass: PING every alive worker, with the send time in microseconds as the token;
         * close_peer() what take_silent() and take_handshake_overdue() list; resubmit_tasks()
         * what take_overdue() returns. The monitor's mutex only guards its sleep; it holds no
         * lock while it calls a brick, the transport or the sink.
         */
        class SUSHIRUNTIME_API ClusterMonitor final
        {
            public:
                /** @brief Builds a monitor over bricks that outlive it; no thread runs yet. */
                ClusterMonitor(WorkerDirectory& directory, PendingTasks& pending,
                               BufferRegistry& registry, BufferExchange& exchange,
                               ITransport& transport, const ClusterMonitorOptions& options);

                /** @brief Calls stop(). */
                ~ClusterMonitor();

                ClusterMonitor(const ClusterMonitor&) = delete;
                ClusterMonitor& operator=(const ClusterMonitor&) = delete;
                ClusterMonitor(ClusterMonitor&&) = delete;
                ClusterMonitor& operator=(ClusterMonitor&&) = delete;

                /**
                 * @brief Starts the thread; called once, after the transport started.
                 * @param sink Receives the tasks taken back; outlives stop().
                 */
                void start(Graph::OffloadSink& sink);

                /**
                 * @brief Wakes the thread and joins it; a second call does nothing.
                 *
                 * The coordinator calls it after PendingTasks::close() and before
                 * ITransport::stop().
                 */
                void stop() noexcept;

                /** @brief Sets the liveness timeout the next pass applies; any thread. */
                void set_liveness_timeout(std::chrono::milliseconds timeout) noexcept;

                /** @brief Sets the task deadline the next pass applies; any thread. */
                void set_task_deadline(std::chrono::milliseconds deadline) noexcept;

                /** @brief Returns the liveness timeout in force, for WELCOME. */
                [[nodiscard]] std::chrono::milliseconds liveness_timeout() const noexcept;

                /** @brief Returns the time between two passes, for WELCOME's ping interval. */
                [[nodiscard]] std::chrono::milliseconds period() const noexcept;

                /** @brief Returns the counters; reads atomics and takes no lock. */
                [[nodiscard]] ClusterMonitorStatistics statistics() const noexcept;

            private:
                /** @brief Runs passes until stop(). */
                void run() noexcept;

                /// @brief Sessions, liveness and handshake deadlines.
                WorkerDirectory& directory_;

                /// @brief The tasks whose limits are checked.
                PendingTasks& pending_;

                /// @brief Bumped for every task taken back.
                BufferRegistry& registry_;

                /// @brief Told to forget every task taken back.
                BufferExchange& exchange_;

                /// @brief Carries the pings and closes peers.
                ITransport& transport_;

                /// @brief The timings; liveness and deadline are mirrored in the atomics.
                ClusterMonitorOptions options_;

                /// @brief Liveness timeout in milliseconds.
                std::atomic<std::int64_t> liveness_timeout_ms_;

                /// @brief Task deadline in milliseconds.
                std::atomic<std::int64_t> task_deadline_ms_;

                /// @brief Guards the sleep between passes and stopping_.
                std::mutex mutex_;

                /// @brief Ends the sleep early in stop().
                std::condition_variable wake_;

                /// @brief Set by stop().
                bool stopping_ = false;

                /// @brief The monitor thread.
                std::thread thread_;

                /// @brief Receives the tasks taken back; set by start().
                Graph::OffloadSink* sink_ = nullptr;

                /// @brief Tasks past the task deadline.
                std::atomic<std::uint64_t> reaped_deadline_{0};

                /// @brief Dispatches nobody confirmed.
                std::atomic<std::uint64_t> reaped_unacknowledged_{0};

                /// @brief Tasks a worker stopped holding.
                std::atomic<std::uint64_t> reaped_lost_result_{0};

                /// @brief Tasks the monitor resubmitted.
                std::atomic<std::uint64_t> redispatched_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
