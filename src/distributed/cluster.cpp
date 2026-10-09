/**************************************************************************/
/* cluster.cpp                                                            */
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
 * @file cluster.cpp
 * @brief Builds a distributed master on a runtime context and removes it in order.
 * @author Mustafa Garip
 * @see src/distributed/README.md
 */

#include <SushiRuntime/distributed/cluster.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>
#include <SushiRuntime/distributed/distributed_coordinator.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief How often wait_for_workers() reads the worker count. */
            constexpr std::chrono::milliseconds K_WORKER_POLL_PERIOD{5};

            /**
             * @brief Stops the coordinator a context holds and removes it from the allocator
             *        and the scheduler.
             * @pre No run is in flight on @p context.
             */
            void release_installed(Execution::RuntimeContext& context)
            {
                if (Graph::OffloadCoordinator* installed = context.offload())
                    installed->stop();

                // The allocator forgets the coordinator before the context can destroy it.
                context.memory().set_release_observer(nullptr);
                context.install_offload(nullptr);
            }
        } // namespace

        /**
         * @brief Holds one master: its buffer registry, its coordinator and where it is installed.
         * @see src/distributed/README.md
         */
        struct Cluster::State
        {
            class Attachment;

            /** @brief Builds the registry, then the coordinator that reads it. */
            State(sushi_ptr<KernelRegistry> kernels,
                  std::shared_ptr<ITransport> transport,
                  std::shared_ptr<IOffloadPolicy> policy)
                : coordinator(std::move(kernels), buffers, std::move(transport), std::move(policy))
            {
            }

            State(const State&) = delete;
            State& operator=(const State&) = delete;

            /** @brief Master-side buffer ids and versions; declared first, destroyed last. */
            BufferRegistry buffers;

            /** @brief The master; it reads buffers until its destructor returns. */
            DistributedCoordinator coordinator;

            /** @brief The context that holds the coordinator; null once it has released it. */
            Execution::RuntimeContext* context = nullptr;
        };

        /**
         * @brief Keeps a State alive while a context holds its coordinator, and records the
         *        release.
         * @see src/distributed/README.md
         */
        class Cluster::State::Attachment final
        {
            public:
                /** @brief Takes a share of the state whose coordinator a context will hold. */
                explicit Attachment(std::shared_ptr<State> state) noexcept
                    : state_(std::move(state))
                {
                }

                /** @brief Marks the state as released by its context. */
                ~Attachment()
                {
                    state_->context = nullptr;
                }

                Attachment(const Attachment&) = delete;
                Attachment& operator=(const Attachment&) = delete;
                Attachment(Attachment&&) = delete;
                Attachment& operator=(Attachment&&) = delete;

            private:
                /** @brief The state this attachment keeps alive. */
                std::shared_ptr<State> state_;
        };

        Cluster::Cluster(std::shared_ptr<State> state) noexcept
            : state_(std::move(state))
        {
        }

        Cluster Cluster::attach(API::Runtime& runtime,
                                std::shared_ptr<ITransport> transport,
                                sushi_ptr<KernelRegistry> kernels,
                                std::shared_ptr<IOffloadPolicy> policy)
        {
            return attach(runtime.unsafe_context(), std::move(transport), std::move(kernels),
                          std::move(policy));
        }

        Cluster Cluster::attach(Execution::RuntimeContext& context,
                                std::shared_ptr<ITransport> transport,
                                sushi_ptr<KernelRegistry> kernels,
                                std::shared_ptr<IOffloadPolicy> policy)
        {
            SR_THROW_IF(!transport, "Cluster::attach requires a transport.");

            if (!policy)
                policy = std::make_shared<LeastLoadedOffloadPolicy>();

            release_installed(context);

            auto state = std::make_shared<State>(std::move(kernels), std::move(transport),
                                                 std::move(policy));
            state->context = &context;
            context.memory().set_release_observer(&state->coordinator);

            // The coordinator is installed before its monitor starts (src/distributed/README.md).
            auto attachment = std::make_shared<State::Attachment>(state);
            context.install_offload(std::shared_ptr<Graph::OffloadCoordinator>(
                std::move(attachment), &state->coordinator));
            state->coordinator.start();

            SR_LOG_INFO("Distributed master attached to the runtime context.");
            return Cluster(std::move(state));
        }

        void Cluster::detach()
        {
            // A non-null context still holds this state's coordinator (src/distributed/README.md).
            if (state_->context != nullptr)
                release_installed(*state_->context);
        }

        bool Cluster::wait_for_workers(int count, std::chrono::milliseconds timeout)
        {
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (alive_workers() < count)
            {
                if (std::chrono::steady_clock::now() >= deadline)
                    return false;
                std::this_thread::sleep_for(K_WORKER_POLL_PERIOD);
            }
            return true;
        }

        int Cluster::alive_workers() const
        {
            return state_->coordinator.alive_worker_count();
        }

        Cluster& Cluster::liveness_timeout(std::chrono::milliseconds timeout)
        {
            state_->coordinator.set_liveness_timeout(timeout);
            return *this;
        }

        Cluster& Cluster::task_deadline(std::chrono::milliseconds deadline)
        {
            state_->coordinator.set_task_deadline(deadline);
            return *this;
        }

        std::uint64_t Cluster::offloaded() const
        {
            return state_->coordinator.offloaded_count();
        }

        std::uint64_t Cluster::declined() const
        {
            return state_->coordinator.declined_count();
        }

        std::uint64_t Cluster::redispatched() const
        {
            return state_->coordinator.redispatched_count();
        }

        std::uint64_t Cluster::stale_writebacks() const
        {
            return state_->coordinator.stale_writeback_count();
        }

        bool Cluster::supports(Graph::OpID op) const
        {
            return state_->coordinator.supports(op);
        }

        void Cluster::bump_buffer(void* address)
        {
            state_->buffers.bump_version(address);
        }

        std::optional<BufferHandle> Cluster::buffer_handle(void* address) const
        {
            return state_->buffers.lookup(address);
        }

    } // namespace Distributed
} // namespace SushiRuntime
