/**************************************************************************/
/* cluster_statistics.hpp                                                 */
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
 * @file cluster_statistics.hpp
 * @brief Declares ClusterStatistics and WorkerSnapshot, what a master reports about itself.
 * @author Mustafa Garip
 * @see modules/extension/cluster_load/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Describes one alive worker at the moment statistics() ran.
         */
        struct WorkerSnapshot
        {
            std::uint32_t id = 0;                  /**< The id WELCOME gave the worker. */
            std::string name;                      /**< The name the worker sent in HELLO. */
            std::string endpoint;                  /**< The worker's numeric `address:port`. */
            std::uint32_t inflight = 0;            /**< Tasks dispatched to it and not settled. */
            std::uint32_t rtt_micros = 0;          /**< Microseconds its last PING took. */
            std::uint64_t free_mem = 0;            /**< Free device memory from its last PONG. */
            std::uint32_t queue_depth = 0;         /**< Tasks waiting on it, from HEARTBEAT. */
            std::uint64_t cache_bytes = 0;         /**< Bytes its buffer cache holds. */
            std::uint64_t ms_since_last_frame = 0; /**< Milliseconds since its last frame. */
        };

        /**
         * @brief Holds a master's counters since attach(), two gauges and its workers.
         *
         * The fields are read one by one, not at one instant. Once the master is detached the
         * counters keep their totals, both gauges read 0 and the worker list is empty.
         */
        struct ClusterStatistics
        {
            std::uint64_t offloaded = 0;                   /**< Dispatches queued to a worker. */
            std::uint64_t declined = 0;                    /**< Offloadable dispatches run here. */
            std::uint64_t completed_remote = 0;            /**< Tasks a worker finished whole. */
            std::uint64_t redispatched = 0;                /**< Tasks taken back and resubmitted. */
            std::uint64_t stale_writebacks = 0;            /**< Output chunks the fence refused. */
            std::uint64_t handshake_rejected_auth = 0;     /**< Peers whose proof failed. */
            std::uint64_t handshake_rejected_version = 0;  /**< Peers of another protocol. */
            std::uint64_t peers_lost = 0;                  /**< Registered workers that left. */
            std::uint64_t peers_closed_malformed = 0;      /**< Peers closed for a bad frame. */
            std::uint64_t tasks_reaped_deadline = 0;       /**< Tasks past the task deadline. */
            std::uint64_t tasks_reaped_unacknowledged = 0; /**< Dispatches no worker confirmed. */
            std::uint64_t tasks_reaped_lost_result = 0;    /**< Tasks a worker stopped holding. */
            std::uint64_t bytes_sent = 0;                  /**< Bytes the transport wrote. */
            std::uint64_t bytes_received = 0;              /**< Bytes the transport read. */
            std::uint64_t chunks_sent = 0;                 /**< Buffer chunks served to workers. */
            std::uint64_t chunks_received = 0;             /**< Output chunks written in place. */
            std::uint64_t transfers_aborted = 0;           /**< Transfers ended by BUFFER_ABORT. */
            std::uint64_t send_queue_full = 0;             /**< Sends refused by a full queue. */
            std::size_t alive_workers = 0;                 /**< Workers registered and answering. */
            std::size_t pending_tasks = 0;                 /**< Tasks out on a worker. */
            std::size_t held_nodes = 0;                    /**< Nodes a rule holds for a worker. */
            std::vector<WorkerSnapshot> workers;           /**< One entry per alive worker. */
        };

    } // namespace Distributed
} // namespace SushiRuntime
