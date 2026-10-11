/**************************************************************************/
/* test_offload_seam_shutdown.cpp                                         */
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
 * @file test_offload_seam_shutdown.cpp
 * @brief Tests that RuntimeContext::shutdown() stops the offload coordinator before the pool.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <gtest/gtest.h>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;

    /** @brief Records, in its first stop(), whether the worker pool still runs a node. */
    class PoolProbingCoordinator final : public SushiRuntime::Internal::Graph::OffloadCoordinator
    {
        public:
            void bind(SushiRuntime::Internal::Graph::OffloadSink& sink) override
            {
                sink_ = &sink;
            }

            void prepare(SushiRuntime::span<const sushi_ptr<Node>>) override
            {

            }

            [[nodiscard]] bool try_offload(Node&) override
            {
                return false;
            }

            void on_local_completion(Node&) noexcept override
            {

            }

            void share(void*, std::size_t) override
            {

            }

            [[nodiscard]] SushiRuntime::Internal::Graph::OffloadStatistics
            statistics() const noexcept override
            {
                return {};
            }

            /** @brief Hands the pool a probe node on the first call and waits for it to run. */
            void stop() noexcept override
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (++stop_calls_ > 1 || sink_ == nullptr)
                    return;

                probe_ = make_sushi<Node>();
                probe_->mode = Node::ExecutionMode::Host;
                probe_->submits_device_work_ = false;
                probe_->native_work = [this] { mark_probe_ran(); };

                // resubmit() consumes one reference, as it does for a node try_offload() accepted.
                probe_->add_ref();
                lock.unlock();
                sink_->resubmit(*probe_);
                lock.lock();
                pool_ran_probe_ = probe_ran_cv_.wait_for(lock, K_PROBE_BUDGET,
                                                         [this] { return probe_ran_; });
            }

            /** @brief Returns how many times stop() was called. */
            [[nodiscard]] int stop_calls() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return stop_calls_;
            }

            /** @brief Returns whether the pool ran the probe before the first stop() returned. */
            [[nodiscard]] bool pool_ran_probe() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return pool_ran_probe_;
            }

        private:
            /** @brief Runs on a pool worker as the probe node's work. */
            void mark_probe_ran()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    probe_ran_ = true;
                }
                probe_ran_cv_.notify_all();
            }

            /// @brief Longest wait for the probe; a stopped pool never runs it.
            static constexpr std::chrono::seconds K_PROBE_BUDGET{10};

            /// @brief Guards every member below.
            mutable std::mutex mutex_;
            /// @brief Signalled when the probe node has run.
            std::condition_variable probe_ran_cv_;
            /// @brief The scheduler, as bind() received it.
            SushiRuntime::Internal::Graph::OffloadSink* sink_ = nullptr;
            /// @brief The node the first stop() hands to the pool.
            sushi_ptr<Node> probe_;
            /// @brief Calls of stop().
            int stop_calls_ = 0;
            /// @brief Set by the probe node on a pool worker.
            bool probe_ran_ = false;
            /// @brief Whether the probe ran within the budget of the first stop().
            bool pool_ran_probe_ = false;
    };
} // namespace

/**
 * @brief shutdown() stops the installed coordinator while the worker pool still runs.
 */
TEST(Integration_OffloadSeam, ShutdownStopsCoordinatorBeforeEngine)
{
    const std::shared_ptr<PoolProbingCoordinator> coordinator =
        std::make_shared<PoolProbingCoordinator>();

    {
        SushiRuntime::Internal::Execution::RuntimeContext context;
        context.install_offload(coordinator);

        context.shutdown();

        EXPECT_EQ(coordinator->stop_calls(), 1) << "shutdown() did not stop the coordinator";
        EXPECT_TRUE(coordinator->pool_ran_probe())
            << "the worker pool had stopped before the coordinator's stop() returned";
    }

    // The destructor stops the coordinator again, which the seam allows; see README.md.
    EXPECT_EQ(coordinator->stop_calls(), 2);
}
