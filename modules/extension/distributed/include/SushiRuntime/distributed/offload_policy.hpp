/**************************************************************************/
/* offload_policy.hpp                                                     */
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
 * @file offload_policy.hpp
 * @brief Declares OffloadPolicy, WorkerLoad and the least-loaded offload policy.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Holds what a policy may know about one worker when it picks.
         */
        struct WorkerLoad
        {
            int           worker_id  = -1;    /**< The worker's id: the value of its PeerID. */
            bool          alive      = false; /**< Registered and heard from within liveness. */
            std::uint32_t inflight   = 0;     /**< Tasks dispatched to it and not settled. */
            std::uint64_t free_mem   = 0;     /**< Free device memory in bytes, from its PONG. */
            std::uint32_t rtt_micros = 0;     /**< Round trip of its last PING in microseconds. */
        };

        /**
         * @brief Chooses the worker that runs an offloadable task, or none.
         *
         * pick_worker() runs on a pool worker at dispatch, under the worker directory's lock,
         * so two calls never overlap. It returns quickly and calls nothing of the Cluster,
         * the coordinator or the transport; a call back deadlocks.
         */
        class SUSHIRUNTIME_API OffloadPolicy
        {
            public:
                /** @brief Destroys the policy through the pointer the cluster owns. */
                virtual ~OffloadPolicy() = default;

                /**
                 * @brief Picks a worker for a task.
                 * @param node The task; read its metadata only.
                 * @param workers The registered workers that have room; valid during the call.
                 * @param est_bytes Bytes of the task's inputs, an upper bound on what crosses.
                 * @return The worker_id of one entry of @p workers, or nothing to run locally.
                 */
                virtual std::optional<int> pick_worker(
                    const Internal::Graph::Node& node, SushiRuntime::span<const WorkerLoad> workers,
                    std::uint64_t est_bytes) = 0;
        };

        /**
         * @brief Picks the worker with the lowest estimated time to finish the task.
         *
         * The estimate adds the round trip, the transfer time and the worker's backlog; a
         * task below the byte threshold stays local.
         */
        class SUSHIRUNTIME_API LeastLoadedOffloadPolicy final : public OffloadPolicy
        {
            public:
                /**
                 * @brief Builds the policy.
                 * @param min_offload_bytes Tasks whose inputs are smaller run locally.
                 */
                explicit LeastLoadedOffloadPolicy(std::uint64_t min_offload_bytes = 64 * 1024)
                    : min_offload_bytes_(min_offload_bytes)
                {

                }

                /** @brief Picks the live worker with the lowest estimate, or none. */
                std::optional<int> pick_worker(const Internal::Graph::Node& node,
                                               SushiRuntime::span<const WorkerLoad> workers,
                                               std::uint64_t est_bytes) override;

            private:
                /** @brief Holds the input size below which a task stays local. */
                std::uint64_t min_offload_bytes_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
