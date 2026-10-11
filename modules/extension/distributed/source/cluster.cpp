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
#include <SushiRuntime/execution/execution_engine.hpp>
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
            /** @brief Reports whether a run is in flight on a context; one atomic load. */
            [[nodiscard]] bool has_run_in_flight(Internal::Execution::RuntimeContext& context)
            {
                return context.execution_engine().run_in_progress();
            }

            /**
             * @brief Stops the coordinator a context holds and removes it from the allocator
             *        and the scheduler.
             * @pre No run is in flight on @p context.
             */
            void release_installed(Internal::Execution::RuntimeContext& context)
            {
                if (Internal::Graph::OffloadCoordinator* installed = context.offload())
                    installed->stop();

                // The allocator forgets the coordinator before the context can destroy it.
                context.memory().set_release_observer(nullptr);
                context.install_offload(nullptr);
            }

            /**
             * @brief Refuses a liveness or handshake timeout a monitor pass would already
             *        count as expired.
             * @param call The public call the value came through, for the message.
             * @param option The option's name, for the message.
             * @throws SushiException When @p value is below K_MINIMUM_TIMEOUT.
             */
            void check_timeout(const char* call, const char* option,
                               std::chrono::milliseconds value)
            {
                SR_THROW_IF(value < K_MINIMUM_TIMEOUT,
                            "{}: {} is {} ms; the smallest value allowed is {} ms.", call, option,
                            value.count(), K_MINIMUM_TIMEOUT.count());
            }

            /**
             * @brief Refuses options a master cannot be attached with.
             * @throws SushiException Naming the option and the value that is allowed.
             */
            void check_options(const Transport& transport, const ClusterOptions& options)
            {
                check_timeout("Cluster::attach", "ClusterOptions::liveness_timeout",
                              options.liveness_timeout);
                check_timeout("Cluster::attach", "ClusterOptions::handshake_timeout",
                              options.handshake_timeout);
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
            State(std::shared_ptr<Transport> transport, ClusterOptions options)
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
            Internal::Execution::RuntimeContext* context = nullptr;
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

        Cluster Cluster::attach(Runtime& runtime, std::shared_ptr<Transport> transport,
                                ClusterOptions options)
        {
            return attach(runtime.unsafe_context(), std::move(transport), std::move(options));
        }

        Cluster Cluster::attach(Internal::Execution::RuntimeContext& context,
                                std::shared_ptr<Transport> transport, ClusterOptions options)
        {
            // The scheduler of a run in flight reads the coordinator this call would replace.
            SR_THROW_IF(has_run_in_flight(context),
                        "Cluster::attach: a run is in flight on the runtime; wait for it to "
                        "return, then attach. Nothing was stopped or installed.");
            SR_THROW_IF(!transport,
                        "Cluster::attach: the transport is null; pass a master-side transport "
                        "that was never started.");
            check_options(*transport, options);

            auto state = std::make_shared<State>(std::move(transport), std::move(options));

            // The new master starts before the installed one is touched, so a failure costs nothing.
            state->coordinator.bind(context.offload_sink());
            const TransportResult started = state->coordinator.start();
            if (!started.is_ok())
            {
                state->coordinator.stop();
                throw SushiException(Error{
                    Errc::invalid_state,
                    "Cluster::attach: the master did not start: " + started.message +
                        (context.offload() != nullptr
                             ? "; the cluster attached before stays installed."
                             : "; nothing is installed on the runtime.")});
            }

            release_installed(context);
            state->context = &context;
            context.memory().set_release_observer(&state->coordinator);

            auto attachment = std::make_shared<State::Attachment>(state);
            context.install_offload(std::shared_ptr<Internal::Graph::OffloadCoordinator>(
                std::move(attachment), &state->coordinator));

            return Cluster(std::move(state));
        }

        void Cluster::detach()
        {
            // A non-null context still holds this state's coordinator.
            Internal::Execution::RuntimeContext* const context = state_->context;
            if (context == nullptr)
                return;

            if (!has_run_in_flight(*context))
            {
                release_installed(*context);
                return;
            }

            // The scheduler of the run in flight reads the installed coordinator until it returns.
            state_->coordinator.stop();
            SR_LOG_WARN("Cluster::detach: a run is in flight on the runtime; the master is "
                        "stopped and its tasks run locally, and it stays installed until "
                        "detach() or attach() is called after the run");
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
            check_timeout("Cluster::liveness_timeout", "the timeout", timeout);
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

        bool Cluster::supports(OperationID op) const
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
