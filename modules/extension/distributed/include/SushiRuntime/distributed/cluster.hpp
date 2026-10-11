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
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>

namespace SushiRuntime
{
    class Runtime;
    namespace Internal::Execution { class RuntimeContext; }

    namespace Distributed
    {
        /** @brief Holds the fewest data bytes one buffer chunk may carry: 4 KiB. */
        inline constexpr std::uint32_t K_MINIMUM_CHUNK_BYTES = 4u * 1024u;

        /** @brief Holds the most data bytes one buffer chunk may carry: 1 MiB less 64. */
        inline constexpr std::uint32_t K_MAXIMUM_CHUNK_BYTES = 1024u * 1024u - 64u;

        /** @brief Holds the data bytes one buffer chunk carries unless told otherwise: 256 KiB. */
        inline constexpr std::uint32_t K_DEFAULT_CHUNK_BYTES = 256u * 1024u;

        /** @brief Holds the shortest liveness or handshake timeout a master takes: 1 ms. */
        inline constexpr std::chrono::milliseconds K_MINIMUM_TIMEOUT{1};

        /**
         * @brief Holds what a master is attached with; moved into Cluster::attach().
         */
        struct ClusterOptions
        {
            /**
             * @brief Holds the key workers must prove they know; empty authenticates nobody.
             *
             * attach() refuses an empty secret on a transport whose exposure() is Network.
             */
            SharedSecret secret;

            /**
             * @brief Chooses the worker of each task; null selects LeastLoadedOffloadPolicy.
             *
             * That default keeps a task whose inputs total under 64 KiB on this machine.
             */
            std::shared_ptr<OffloadPolicy> policy;

            /** @brief Lives as long as the master, which reads no kernel from it; may be null. */
            sushi_ptr<KernelRegistry> kernels;

            /**
             * @brief Holds the silence after which a worker counts as lost; sent in WELCOME.
             *
             * attach() refuses a value below K_MINIMUM_TIMEOUT.
             */
            std::chrono::milliseconds liveness_timeout{5000};

            /** @brief Bounds a kernel's run on a worker; the one field where 0 sets no bound. */
            std::chrono::milliseconds task_deadline{30000};

            /**
             * @brief Bounds the time a new connection has to finish the handshake.
             *
             * attach() refuses a value below K_MINIMUM_TIMEOUT.
             */
            std::chrono::milliseconds handshake_timeout{5000};

            /** @brief Holds the data bytes per buffer chunk, within the two limits above. */
            std::uint32_t chunk_bytes = K_DEFAULT_CHUNK_BYTES;
        };

        /**
         * @brief Attaches a distributed master to a runtime and reads its knobs and counters.
         *
         * Every copy of a handle names the same master, and a move copies. Destroying a handle
         * leaves the master attached: the runtime releases it when destroyed, detach() sooner.
         * @pre attach(), detach() and the runtime's destruction are ordered by the caller.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API Cluster final
        {
            public:
                /**
                 * @brief Builds a master, installs it as the runtime's offload coordinator and
                 *        starts it and the transport.
                 * @pre @p transport was never started.
                 * @throws SushiException When a run is in flight on @p runtime, @p transport
                 *         is null, an option is refused or the transport does not start; a
                 *         cluster attached before stays installed and keeps running.
                 */
                [[nodiscard]] static Cluster attach(Runtime& runtime,
                                                    std::shared_ptr<Transport> transport,
                                                    ClusterOptions options = ClusterOptions());

                /**
                 * @brief Builds a master on a bare context; the Runtime overload forwards here.
                 * @param transport A master-side transport that was never started.
                 * @throws SushiException As the Runtime overload.
                 */
                [[nodiscard]] static Cluster attach(Internal::Execution::RuntimeContext& context,
                                                    std::shared_ptr<Transport> transport,
                                                    ClusterOptions options = ClusterOptions());

                /** @brief Copies the handle; both name the same master. */
                Cluster(const Cluster&) = default;

                /** @brief Makes this handle name the master of another; detaches nothing. */
                Cluster& operator=(const Cluster&) = default;

                /** @brief Destroys the handle; the master stays attached. */
                ~Cluster() = default;

                /**
                 * @brief Stops the master and removes it from its runtime; does nothing once the
                 *        runtime has released it.
                 *
                 * Tasks out on a worker come back and run on this machine. During a run in
                 * flight the stopped master stays installed and offloads nothing; a detach()
                 * or attach() after that run, or the runtime's destruction, removes it.
                 */
                void detach();

                /**
                 * @brief Blocks until @p count workers are registered, or the timeout.
                 * @return True when the count was reached in time; false once detached.
                 */
                [[nodiscard]] bool wait_for_workers(std::size_t count,
                                                    std::chrono::milliseconds timeout);

                /** @brief Returns the number of answering workers; 0 once detached. */
                [[nodiscard]] std::size_t alive_workers() const;

                /**
                 * @brief Sets how long a worker may stay silent before it is closed.
                 *
                 * Workers registered earlier keep the value their WELCOME carried.
                 * @param timeout At least K_MINIMUM_TIMEOUT.
                 * @throws SushiException With invalid_state for a shorter value.
                 */
                Cluster& liveness_timeout(std::chrono::milliseconds timeout);

                /**
                 * @brief Sets the bound on a kernel's run on a worker; 0 sets no bound.
                 *
                 * A dispatch no worker confirms and a result that is lost are taken back
                 * whatever this value is.
                 */
                Cluster& task_deadline(std::chrono::milliseconds deadline);

                /** @brief Returns every counter, the two gauges and one entry per worker. */
                [[nodiscard]] ClusterStatistics statistics() const;

                /** @brief Returns statistics().offloaded without building the worker list. */
                [[nodiscard]] std::uint64_t offloaded() const;

                /** @brief Returns statistics().declined without building the worker list. */
                [[nodiscard]] std::uint64_t declined() const;

                /** @brief Returns statistics().redispatched without building the worker list. */
                [[nodiscard]] std::uint64_t redispatched() const;

                /** @brief Returns statistics().stale_writebacks without building the list. */
                [[nodiscard]] std::uint64_t stale_writebacks() const;

                /** @brief Reports whether an alive worker has a kernel for an operation. */
                [[nodiscard]] bool supports(Internal::Graph::OpID op) const;

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
