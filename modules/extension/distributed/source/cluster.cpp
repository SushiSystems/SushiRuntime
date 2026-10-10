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
 * @see modules/extension/distributed/README.md
 */

#include <SushiRuntime/distributed/cluster.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>

#include "buffer_registry.hpp"
#include "distributed_coordinator.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
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

            /**
             * @brief Refuses options a master cannot be attached with.
             * @throws Core::SushiException Naming the option and the value that is allowed.
             */
            void check_options(const ITransport& transport, const ClusterOptions& options)
            {
                SR_THROW_IF(options.secret.empty() &&
                                transport.exposure() == TransportExposure::Network,
                            "Cluster::attach: the transport can be reached from other machines "
                            "and ClusterOptions::secret is empty; load one with "
                            "SharedSecret::from_file(), or bind the master to a loopback "
                            "address.");
                SR_THROW_IF(options.chunk_bytes < K_MINIMUM_CHUNK_BYTES ||
                                options.chunk_bytes > K_MAXIMUM_CHUNK_BYTES,
                            "Cluster::attach: ClusterOptions::chunk_bytes is {}; it must be "
                            "between {} and {}.",
                            options.chunk_bytes, K_MINIMUM_CHUNK_BYTES, K_MAXIMUM_CHUNK_BYTES);
            }
        } // namespace

        /**
         * @brief Holds one master: its buffer registry, its coordinator and where it is installed.
         * @see modules/extension/distributed/README.md
         */
        struct Cluster::State
        {
            class Attachment;

            /** @brief Builds the registry, then the coordinator that reads it. */
            State(std::shared_ptr<ITransport> transport, ClusterOptions options)
                : coordinator(buffers, std::move(transport), std::move(options))
            {

            }

            State(const State&) = delete;
            State& operator=(const State&) = delete;
            State(State&&) = delete;
            State& operator=(State&&) = delete;

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
         * @see modules/extension/distributed/README.md
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

        Cluster Cluster::attach(API::Runtime& runtime, std::shared_ptr<ITransport> transport,
                                ClusterOptions options)
        {
            return attach(runtime.unsafe_context(), std::move(transport), std::move(options));
        }

        Cluster Cluster::attach(Execution::RuntimeContext& context,
                                std::shared_ptr<ITransport> transport, ClusterOptions options)
        {
            SR_THROW_IF(!transport,
                        "Cluster::attach: the transport is null; pass a master-side transport "
                        "that was never started.");
            check_options(*transport, options);

            release_installed(context);

            auto state = std::make_shared<State>(std::move(transport), std::move(options));
            state->context = &context;
            context.memory().set_release_observer(&state->coordinator);

            // The scheduler binds the coordinator here, before its transport and monitor start.
            auto attachment = std::make_shared<State::Attachment>(state);
            context.install_offload(std::shared_ptr<Graph::OffloadCoordinator>(
                std::move(attachment), &state->coordinator));

            const TransportResult started = state->coordinator.start();
            if (!started.is_ok())
            {
                // Releasing the context's share destroys the attachment; the state dies here.
                release_installed(context);
                throw Core::SushiException(Core::Error{
                    Core::Errc::invalid_state,
                    "Cluster::attach: the master did not start: " + started.message +
                        "; nothing stays installed on the runtime."});
            }

            return Cluster(std::move(state));
        }

        void Cluster::detach()
        {
            // A non-null context still holds this state's coordinator.
            if (state_->context != nullptr)
                release_installed(*state_->context);
        }

        bool Cluster::wait_for_workers(std::size_t count, std::chrono::milliseconds timeout)
        {
            return state_->coordinator.wait_for_workers(count, timeout);
        }

        std::size_t Cluster::alive_workers() const
        {
            return state_->coordinator.statistics().alive_workers;
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

        ClusterStatistics Cluster::statistics() const
        {
            return state_->coordinator.cluster_statistics();
        }

        std::uint64_t Cluster::offloaded() const
        {
            return state_->coordinator.statistics().offloaded;
        }

        std::uint64_t Cluster::declined() const
        {
            return state_->coordinator.statistics().declined;
        }

        std::uint64_t Cluster::redispatched() const
        {
            return state_->coordinator.statistics().redispatched;
        }

        std::uint64_t Cluster::stale_writebacks() const
        {
            return state_->coordinator.statistics().stale_writebacks;
        }

        bool Cluster::supports(Graph::OpID op) const
        {
            return state_->coordinator.supports(op);
        }

        void Cluster::bump_buffer(void* address)
        {
            void* const addresses[] = {address};
            state_->buffers.bump_addresses(SushiRuntime::span<void* const>(addresses, 1));
        }

        std::optional<BufferHandle> Cluster::buffer_handle(void* address) const
        {
            return state_->buffers.lookup(address);
        }

    } // namespace Distributed
} // namespace SushiRuntime
