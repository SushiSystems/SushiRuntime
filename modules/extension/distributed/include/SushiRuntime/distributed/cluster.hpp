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
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>

namespace SushiRuntime
{
    class Runtime;

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
                 * @pre @p transport is not null and was never started.
                 * @return The handle, or InvalidState when a run is in flight on @p runtime, an option
                 *         is refused or the transport does not start, and Unsupported when the platform
                 *         has no random source; a cluster attached before stays installed and keeps running.
                 */
                [[nodiscard]] static Result<Cluster>
                attach(Runtime& runtime, std::shared_ptr<Transport> transport,
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
                 * @return Nothing, or InvalidState for a value below K_MINIMUM_TIMEOUT, which
                 *         changes nothing.
                 */
                [[nodiscard]] Result<void> liveness_timeout(std::chrono::milliseconds timeout);

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
                [[nodiscard]] bool supports(OperationID op) const;

                /**
                 * @brief Makes a buffer known to the workers, so a node that names an operation and
                 *        touches it may run on one; does nothing for an empty buffer.
                 * @return buffer.
                 * @pre A buffer that owns memory is shared or host USM, and is shared before
                 *      the graph that touches it first runs.
                 * @see modules/extension/distributed/README.md
                 */
                template <typename T>
                Buffer<T>& share(Buffer<T>& buffer)
                {
                    share_handle("Cluster::share(buffer)", buffer.data(),
                                 buffer.size() * sizeof(T), buffer.residency(),
                                 buffer.is_host_accessible());
                    return buffer;
                }

                /**
                 * @brief Shares a buffer as share(Buffer&) does, then returns it by value.
                 * @return The buffer, moved out of the argument.
                 * @pre A buffer that owns memory is shared or host USM.
                 */
                template <typename T>
                [[nodiscard]] Buffer<T> share(Buffer<T>&& buffer)
                {
                    share(buffer);
                    return std::move(buffer);
                }

                /**
                 * @brief Makes both fields of a state known to the workers; does nothing for
                 *        an empty state.
                 * @return state.
                 * @pre A state that owns memory is shared or host USM, and is shared before
                 *      the graph that touches it first runs.
                 * @see modules/extension/distributed/README.md
                 */
                template <typename T>
                State<T>& share(State<T>& state)
                {
                    const std::size_t bytes = state.size() * sizeof(T);
                    share_handle("Cluster::share(state)", state.current(), bytes,
                                 state.residency(), state.is_host_accessible());
                    share_handle("Cluster::share(state)", state.next(), bytes,
                                 state.residency(), state.is_host_accessible());
                    return state;
                }

                /**
                 * @brief Shares a state as share(State&) does, then returns it by value.
                 * @return The state, moved out of the argument.
                 * @pre A state that owns memory is shared or host USM.
                 */
                template <typename T>
                [[nodiscard]] State<T> share(State<T>&& state)
                {
                    share(state);
                    return std::move(state);
                }

                /**
                 * @brief Makes a USM allocation known to the workers; an address shared again
                 *        stays one allocation to them.
                 * @param address Base of a live allocation of at least @p bytes that the host
                 *        can address.
                 */
                void share(void* address, std::size_t bytes);

                /** @brief Advances the version of the buffer shared at an address, if any. */
                void bump_buffer(void* address);

                /**
                 * @brief Returns the current handle of the buffer shared at an address.
                 * @return Nothing when the address was never shared or has been released.
                 */
                [[nodiscard]] std::optional<BufferHandle> buffer_handle(void* address) const;

            private:
                struct State;

                /**
                 * @brief Shares the allocation of a handle after checking the host can
                 *        address it; does nothing for a null address.
                 * @param call Names the public call in the message of a failed check.
                 */
                void share_handle(const char* call, void* address, std::size_t bytes,
                                  Residency residency, bool is_host_accessible);

                /** @brief Wraps the state attach() built. */
                explicit Cluster(std::shared_ptr<State> state) noexcept;

                /** @brief The registry and the master; shared by every copy of the handle. */
                std::shared_ptr<State> state_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
