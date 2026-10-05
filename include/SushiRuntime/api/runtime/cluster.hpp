/****************************************************************/
/* cluster.hpp                                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
         * @brief Fluent handle to a Master-mode distributed cluster.
         *
         * Returned by Runtime::cluster(), which promotes the runtime to Master and
         * wires the transport. This handle bundles the cluster's tuning knobs and
         * telemetry so callers do not reach into RuntimeContext's distributed_*
         * surface directly. Offloadable tasks are still added with op_id-bearing
         * task metadata; this handle governs the cluster, not the graph.
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
