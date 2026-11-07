/**************************************************************************/
/* accepting_offload_coordinator.hpp                                      */
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
 * @file accepting_offload_coordinator.hpp
 * @brief Declares AcceptingOffloadCoordinator, which takes every node and completes it on a thread it owns.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>

namespace TestSupport
{

/**
 * @brief Takes every node it is offered and completes it, unrun, from a worker thread it owns.
 *
 * prepare() marks every node offloadable, so each dispatch of a run reaches try_offload(). The
 * worker hands each accepted node to the bound sink's complete_remote(); the node's task never runs.
 * stop() and the destructor join the worker.
 */
class AcceptingOffloadCoordinator final : public SushiRuntime::Internal::Graph::OffloadCoordinator
{
    public:
        /** @brief Starts the worker, which waits for the first accepted node. */
        AcceptingOffloadCoordinator() : worker_([this] { complete_accepted_nodes(); })
        {

        }

        /** @brief Stops the coordinator and joins its worker. */
        ~AcceptingOffloadCoordinator() override
        {
            stop();
        }

        AcceptingOffloadCoordinator(const AcceptingOffloadCoordinator&) = delete;
        AcceptingOffloadCoordinator& operator=(const AcceptingOffloadCoordinator&) = delete;
        AcceptingOffloadCoordinator(AcceptingOffloadCoordinator&&) = delete;
        AcceptingOffloadCoordinator& operator=(AcceptingOffloadCoordinator&&) = delete;

        /**
         * @brief Keeps the sink the worker completes nodes through.
         * @param sink Outlives every node the coordinator accepts.
         */
        void bind(SushiRuntime::Internal::Graph::OffloadSink& sink) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sink_ = &sink;
        }

        /**
         * @brief Marks every node of the compiled graph offloadable.
         * @param nodes Borrowed for the call; a null slot is skipped.
         */
        void prepare(SushiRuntime::span<const SushiRuntime::sushi_ptr<SushiRuntime::Internal::Graph::Node>> nodes,
                     const std::shared_ptr<const SushiRuntime::PlacementRules>&) override
        {
            for (const SushiRuntime::sushi_ptr<SushiRuntime::Internal::Graph::Node>& node : nodes)
                if (node)
                    node->offload().offloadable = true;
        }

        /**
         * @brief Queues the node for the worker, unless the coordinator has stopped.
         * @return True when the node was queued; false after stop().
         */
        [[nodiscard]] bool try_offload(SushiRuntime::Internal::Graph::Node& node) override
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopped_)
                    return false;
                accepted_.push_back(&node);
            }
            accepted_signal_.notify_one();
            return true;
        }

        /** @brief Ignores a node the pool ran itself. */
        void on_local_completion(SushiRuntime::Internal::Graph::Node&) noexcept override
        {

        }

        /** @brief Refuses every later node, lets the worker complete the queued ones and joins it. */
        void stop() noexcept override
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopped_ = true;
            }
            accepted_signal_.notify_all();
            if (worker_.joinable())
                worker_.join();
        }

    private:
        /** @brief Completes each accepted node through the sink until stop() leaves the queue empty. */
        void complete_accepted_nodes()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            for (;;)
            {
                accepted_signal_.wait(lock, [this] { return stopped_ || !accepted_.empty(); });
                if (accepted_.empty())
                    return;

                std::vector<SushiRuntime::Internal::Graph::Node*> batch;
                batch.swap(accepted_);
                SushiRuntime::Internal::Graph::OffloadSink* const sink = sink_;
                lock.unlock();
                for (SushiRuntime::Internal::Graph::Node* node : batch)
                    sink->complete_remote(*node);
                lock.lock();
            }
        }

        std::mutex mutex_;                                             /**< Guards every member below but the worker. */
        std::condition_variable accepted_signal_;                      /**< Signalled by an accepted node and by stop(). */
        SushiRuntime::Internal::Graph::OffloadSink* sink_ = nullptr;   /**< The sink bind() was given. */
        std::vector<SushiRuntime::Internal::Graph::Node*> accepted_;   /**< Nodes accepted and not yet completed. */
        bool stopped_ = false;                                         /**< Set by stop(); no node is accepted after it. */
        std::thread worker_;                                           /**< Runs complete_accepted_nodes(); declared last, so it starts last. */
};

} // namespace TestSupport
