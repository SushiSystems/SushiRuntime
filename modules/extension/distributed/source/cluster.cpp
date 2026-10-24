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

#include <SushiLog/text/text_format.hpp>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/distributed/distributed_log_category.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>

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

            /** @brief Logs why a call is refused and returns the reason as InvalidState. */
            ErrorWrapper<Error> refusal(std::string message)
            {
                SR_LOG_ERROR(K_DISTRIBUTED_LOG_CATEGORY, "{}", message);
                return make_error(Error{ErrorCode::InvalidState, std::move(message)});
            }

            /**
             * @brief Refuses a liveness or handshake timeout a monitor pass would already
             *        count as expired.
             * @param call The public call the value came through, for the message.
             * @param option The option's name, for the message.
             * @return Nothing, or InvalidState when @p value is below K_MINIMUM_TIMEOUT.
             */
            Result<void> check_timeout(const char* call, const char* option,
                                       std::chrono::milliseconds value)
            {
                if (value < K_MINIMUM_TIMEOUT)
                    return refusal(SushiLog::format_text(
                        "{}: {} is {} ms; the smallest value allowed is {} ms.", call, option,
                        value.count(), K_MINIMUM_TIMEOUT.count()));
                return {};
            }

            /**
             * @brief Refuses options a master cannot be attached with.
             * @return Nothing, or InvalidState naming the option and the value that is allowed.
             */
            Result<void> check_options(const Transport& transport, const ClusterOptions& options)
            {
                Result<void> checked = check_timeout(
                    "Cluster::attach", "ClusterOptions::liveness_timeout",
                    options.liveness_timeout);
                if (!checked)
                    return checked;
                checked = check_timeout("Cluster::attach", "ClusterOptions::handshake_timeout",
                                        options.handshake_timeout);
                if (!checked)
                    return checked;
                if (options.secret.empty() && transport.exposure() == TransportExposure::Network)
                    return refusal(
                        "Cluster::attach: the transport can be reached from other machines "
                        "and ClusterOptions::secret is empty; load one with "
                        "SharedSecret::from_file(), or bind the master to a loopback address.");
                if (options.chunk_bytes < K_MINIMUM_CHUNK_BYTES ||
                    options.chunk_bytes > K_MAXIMUM_CHUNK_BYTES)
                    return refusal(SushiLog::format_text(
                        "Cluster::attach: ClusterOptions::chunk_bytes is {}; it must be "
                        "between {} and {}.",
                        options.chunk_bytes, K_MINIMUM_CHUNK_BYTES, K_MAXIMUM_CHUNK_BYTES));
                return {};
            }

            /** @brief Returns the session identifier of one master, drawn at random. */
            Result<std::uint64_t> draw_session_id()
            {
                const Result<HandshakeNonce> nonce = make_nonce();
                if (!nonce)
                    return make_error(nonce.error());

                std::uint64_t session = 0;
                for (std::size_t index = 0; index < sizeof(session); ++index)
                    session |= static_cast<std::uint64_t>(nonce->bytes[index]) << (8 * index);
                return session;
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
            State(std::shared_ptr<Transport> transport, ClusterOptions options,
                  std::uint64_t session_id, const Internal::Execution::RuntimeContext& context,
                  NodeFacts master)
                : coordinator(buffers, std::move(transport), std::move(options), session_id,
                              context, std::move(master))
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

        Result<Cluster> Cluster::attach(Runtime& runtime, std::shared_ptr<Transport> transport,
                                        ClusterOptions options)
        {
            Internal::Execution::RuntimeContext& context =
                SushiRuntime::Internal::ContextAccess::context(runtime);
            SR_VERIFY(transport != nullptr,
                      "Cluster::attach: the transport is null; pass a master-side transport "
                      "that was never started.");

            // The scheduler of a run in flight reads the coordinator this call would replace.
            if (has_run_in_flight(context))
                return refusal("Cluster::attach: a run is in flight on the runtime; wait for it "
                               "to return, then attach. Nothing was stopped or installed.");
            Result<void> checked = check_options(*transport, options);
            if (!checked)
                return make_error(std::move(checked.error()));
            Result<std::uint64_t> session_id = draw_session_id();
            if (!session_id)
                return make_error(std::move(session_id.error()));

            auto state = std::make_shared<State>(std::move(transport), std::move(options),
                                                 *session_id, context, runtime.topology());

            // The new master starts before the installed one is touched, so a failure costs nothing.
            state->coordinator.bind(context.offload_sink());
            const TransportResult started = state->coordinator.start();
            if (!started.is_ok())
            {
                state->coordinator.stop();
                return refusal("Cluster::attach: the master did not start: " + started.message +
                               (context.offload() != nullptr
                                    ? "; the cluster attached before stays installed."
                                    : "; nothing is installed on the runtime."));
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
            SR_LOG_WARN(K_DISTRIBUTED_LOG_CATEGORY, "Cluster::detach: a run is in flight on the runtime; the master is "
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

        Result<void> Cluster::liveness_timeout(std::chrono::milliseconds timeout)
        {
            Result<void> checked = check_timeout("Cluster::liveness_timeout", "the timeout", timeout);
            if (!checked)
                return checked;
            state_->coordinator.set_liveness_timeout(timeout);
            return {};
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

        void Cluster::share(void* address, std::size_t bytes)
        {
            state_->coordinator.share(address, bytes);
        }

        void Cluster::share_handle(const char* call, void* address, std::size_t bytes,
                                   Residency residency, bool is_host_accessible)
        {
            if (address == nullptr)
                return;
            SR_VERIFY(is_host_accessible,
                      (std::string(call) + ": the handle has " + residency_name(residency) +
                       " residency, and the host cannot address it; allocate the handle with a "
                       "host-accessible residency, Residency::Shared or Residency::Host.")
                          .c_str());
            share(address, bytes);
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
