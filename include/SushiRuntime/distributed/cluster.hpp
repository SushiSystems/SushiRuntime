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
 * @brief Declares Cluster, the handle to the distributed master attached to a runtime.
 * @author Mustafa Garip
 * @see include/SushiRuntime/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/protocol.hpp>

namespace SushiRuntime
{
    namespace API { class Runtime; }
    namespace Execution { class RuntimeContext; }

    namespace Distributed
    {
        /**
         * @brief Attaches a distributed master to a runtime and reads its knobs and counters.
         *
         * Every copy of a handle names the same master. Destroying a handle leaves the master
         * attached: the runtime releases it when destroyed, detach() sooner.
         * @pre attach(), detach() and the runtime's destruction are ordered by the caller.
         * @see include/SushiRuntime/distributed/README.md
         */
        class SUSHIRUNTIME_API Cluster final
        {
            public:
                /**
                 * @brief Builds a master on a runtime, installs it as the runtime's offload
                 *        coordinator and starts it.
                 * @pre No run is in flight on @p runtime.
                 * @param transport Connected master-side transport, one peer per worker.
                 * @param policy Offload policy; null selects LeastLoadedOffloadPolicy.
                 * @throws Core::SushiException When @p transport is null.
                 */
                static Cluster attach(API::Runtime& runtime,
                                      std::shared_ptr<ITransport> transport,
                                      sushi_ptr<KernelRegistry> kernels,
                                      std::shared_ptr<IOffloadPolicy> policy = nullptr);

                /**
                 * @brief Builds a master on a bare context; the Runtime overload forwards here.
                 * @pre No run is in flight on @p context.
                 * @param transport Connected master-side transport, one peer per worker.
                 * @param policy Offload policy; null selects LeastLoadedOffloadPolicy.
                 * @throws Core::SushiException When @p transport is null.
                 */
                static Cluster attach(Execution::RuntimeContext& context,
                                      std::shared_ptr<ITransport> transport,
                                      sushi_ptr<KernelRegistry> kernels,
                                      std::shared_ptr<IOffloadPolicy> policy = nullptr);

                /**
                 * @brief Stops the master and removes it from its runtime; does nothing once the
                 *        runtime has released it.
                 * @pre No run is in flight on the runtime.
                 * @see include/SushiRuntime/distributed/README.md
                 */
                void detach();

                /**
                 * @brief Blocks until @p count workers have registered, or the timeout.
                 * @return True when the cluster reached @p count live workers in time.
                 */
                bool wait_for_workers(int count, std::chrono::milliseconds timeout);

                /** @brief Returns the number of live, registered workers. */
                [[nodiscard]] int alive_workers() const;

                /** @brief Sets how long a worker may stay silent before it is declared dead. */
                Cluster& liveness_timeout(std::chrono::milliseconds timeout);

                /** @brief Sets the per-task offload deadline; zero disables it. */
                Cluster& task_deadline(std::chrono::milliseconds deadline);

                /** @brief Returns the number of tasks shipped to workers. */
                [[nodiscard]] std::uint64_t offloaded() const;

                /** @brief Returns the number of offloadable tasks the policy kept local. */
                [[nodiscard]] std::uint64_t declined() const;

                /** @brief Returns the number of tasks re-injected after a failure or a deadline. */
                [[nodiscard]] std::uint64_t redispatched() const;

                /** @brief Returns the number of writebacks the version fence rejected. */
                [[nodiscard]] std::uint64_t stale_writebacks() const;

                /** @brief Reports whether a live worker runs an operation. */
                [[nodiscard]] bool supports(Graph::OpID op) const;

                /** @brief Advances the version of the buffer shared at an address, if any. */
                void bump_buffer(void* address);

                /**
                 * @brief Returns the current handle of the buffer shared at an address.
                 * @return Nothing when the address was never shared or has been released.
                 */
                [[nodiscard]] std::optional<BufferHandle> buffer_handle(void* address) const;

            private:
                struct State;

                /** @brief Wraps the state attach() built. */
                explicit Cluster(std::shared_ptr<State> state) noexcept;

                /** @brief The registry and the master; shared by every copy of the handle. */
                std::shared_ptr<State> state_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
