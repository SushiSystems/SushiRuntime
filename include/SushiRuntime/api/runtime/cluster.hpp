/**************************************************************************/
/* cluster.hpp                                                            */
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
 * @file cluster.hpp
 * @brief Declares Cluster, the fluent handle to a Master-mode distributed cluster.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace API
    {
        class Runtime;

#ifdef SR_DISTRIBUTED
        /**
         * @brief Bundles the tuning knobs and telemetry of a Master-mode distributed cluster.
         * @see include/SushiRuntime/api/runtime/README.md
         */
        class Cluster
        {
            public:
                /**
                 * @brief Blocks until @p n workers have registered, or the timeout.
                 * @return true if the cluster reached @p n live workers in time.
                 */
                bool wait_for_workers(int n, std::chrono::milliseconds timeout)
                {
                    const auto deadline = std::chrono::steady_clock::now() + timeout;
                    while (ctx_->distributed_alive_workers() < n)
                    {
                        if (std::chrono::steady_clock::now() >= deadline)
                            return false;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    return true;
                }

                /** @brief Live (registered) worker count. */
                int alive_workers() const { return ctx_->distributed_alive_workers(); }

                /** @brief Sets the worker-silence timeout (see RuntimeContext). */
                Cluster& liveness_timeout(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_liveness_timeout(t);
                    return *this;
                }

                /** @brief Sets the per-task offload deadline (see RuntimeContext). */
                Cluster& task_deadline(std::chrono::milliseconds t)
                {
                    ctx_->distributed_set_task_deadline(t);
                    return *this;
                }

                /** @brief Tasks shipped to workers. */
                std::uint64_t offloaded() const { return ctx_->distributed_offloaded_count(); }
                /** @brief Offloadable tasks the policy kept local. */
                std::uint64_t declined() const { return ctx_->distributed_declined_count(); }
                /** @brief Tasks re-injected after a worker death / deadline / failure. */
                std::uint64_t redispatched() const { return ctx_->distributed_redispatched_count(); }
                /** @brief Writebacks rejected by the version fence. */
                std::uint64_t stale_writebacks() const { return ctx_->distributed_stale_writeback_count(); }

            private:
                friend class Runtime;
                explicit Cluster(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)) {}
                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };
#endif // SR_DISTRIBUTED

    } // namespace API
} // namespace SushiRuntime
