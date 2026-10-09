/**************************************************************************/
/* runtime_context.hpp                                                    */
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
 * @file runtime_context.hpp
 * @brief Declares RuntimeContext, which builds the runtime's parts and owns the execution engine.
 * @author Mustafa Garip
 * @see include/SushiRuntime/execution/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#ifdef SR_DISTRIBUTED
#include <chrono>
#include <cstdint>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/distributed/protocol.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/distributed_coordinator.hpp>
#endif

namespace SushiRuntime
{
    namespace Execution
    {
        /**
         * @brief Builds the topology view, the device queues and the USM allocator, and owns
         *        the execution engine that runs on them.
         *
         * Constructed once per process and shared (via shared_ptr) between the fluent
         * API layer and all graphs it produces. Uses Pimpl to keep the ABI stable.
         * In Standalone mode (default) carries zero distributed overhead.
         */
        class SUSHIRUNTIME_API RuntimeContext
        {
            public:
                /**
                 * @brief Creates a runtime context and scans for hardware matching config.
                 * @param provider Topology source; null selects the hwloc default.
                 * @param distance_model NUMA distance model; null selects the hwloc default.
                 * @throws Core::SushiException With Errc::unsupported when no device passes config.
                 * @see include/SushiRuntime/execution/README.md
                 */
                explicit RuntimeContext(const Topology::HardwareFilterConfig& config = {},
                                        std::shared_ptr<Topology::ITopologyProvider> provider = nullptr,
                                        std::shared_ptr<Topology::INumaDistanceModel> distance_model = nullptr,
                                        const RuntimeSettings& settings = {});

                /** @brief Stops the engine, then waits for every device queue. */
                ~RuntimeContext();

                /** @brief Copy and move are disabled to keep state integrity. */
                RuntimeContext(const RuntimeContext&) = delete;
                RuntimeContext& operator=(const RuntimeContext&) = delete;

                /**
                 * @brief Returns the runtime-wide settings this context was created with.
                 * @return A reference valid for this context's lifetime.
                 * @see include/SushiRuntime/execution/README.md
                 */
                const RuntimeSettings& settings() const noexcept;

                /** @brief Returns the core-to-NUMA map and the NUMA distances. */
                [[nodiscard]] const Topology::TopologyView& topology() const noexcept;

                /** @brief Returns the device inventory and every SYCL queue built on it. */
                [[nodiscard]] Device::DeviceQueueSet& devices() noexcept;

                /** @brief Returns the allocator that records and releases this context's USM. */
                [[nodiscard]] Memory::UsmAllocator& memory() noexcept;

                /**
                 * @brief Returns the allocator as a shared handle, for an owner that may outlive
                 *        this context.
                 */
                [[nodiscard]] const std::shared_ptr<Memory::UsmAllocator>& shared_memory() const noexcept;

                /** @brief Returns the byte counters of explicit host and device transfers. */
                [[nodiscard]] Observability::TransferCounters& transfers() noexcept;

                /**
                 * @brief Returns the execution engine owned by this context.
                 *
                 * Separates thread orchestration from DAG construction: TaskGraph
                 * builds the plan; the engine drives it through the worker pool.
                 *
                 * @return IExecutionEngine& Reference to the active engine.
                 */
                IExecutionEngine& get_execution_engine();

                /**
                 * @brief Explicitly shuts down the execution engine and its worker pool.
                 *
                 * Calling this before the RuntimeContext destructor is optional but
                 * allows controlled, ordered teardown in sensitive environments.
                 * The destructor will call this automatically if not done explicitly.
                 */
                void shutdown();

                // Every member from here to the template malloc_shared forwards to a part above.

                /** @brief Returns settings().profiling. */
                bool profiling_enabled() const noexcept;

                /** @brief Forwards to transfers().add_device_to_host(bytes). */
                void add_device_to_host_bytes(std::size_t bytes) noexcept;

                /** @brief Forwards to transfers().add_host_to_device(bytes). */
                void add_host_to_device_bytes(std::size_t bytes) noexcept;

                /** @brief Forwards to transfers().device_to_host(). */
                std::size_t device_to_host_bytes() const noexcept;

                /** @brief Forwards to transfers().host_to_device(). */
                std::size_t host_to_device_bytes() const noexcept;

                /**
                 * @brief Forwards to devices().queue(xpu_index).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Forwards to devices().queue(xpu_index, {false, true}).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& profiling_queue(size_t xpu_index = 0);

                /** @brief Forwards to devices().has_profiling_queue(xpu_index). */
                bool has_profiling_queue(size_t xpu_index) const noexcept;

                /**
                 * @brief Forwards to devices().queue(xpu_index, {true, profiling}).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& chain_queue(size_t xpu_index, bool profiling);

                /** @brief Forwards to devices().count(). */
                size_t device_count() const;

                /**
                 * @brief Forwards to devices().is_gpu(xpu_index).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                bool device_is_gpu(size_t xpu_index) const;

                /** @brief Forwards to devices().queue_or_numa_local(std::nullopt, numa_id). */
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief Forwards to devices().queue_or_numa_local(memory().owning_device(ptr),
                 *        fallback_numa).
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& get_queue_for_ptr(void* ptr, int fallback_numa = 0);

                /**
                 * @brief Forwards to memory().device_of(ptr).
                 * @return The device index, or -1 where device_of() returns nothing.
                 */
                int context_index_for_ptr(void* ptr) noexcept;

                /**
                 * @brief Forwards to memory().allocate() with UsmKind::Shared on
                 *        devices().device_for_numa(numa_id).
                 * @see include/SushiRuntime/execution/README.md
                 */
                void* malloc_shared(size_t bytes, int numa_id = 0);

                /**
                 * @brief Forwards to memory().allocate() with UsmKind::Device on
                 *        devices().device_for_numa(numa_id).
                 */
                void* malloc_device(size_t bytes, int numa_id = 0);

                /** @brief Forwards to memory().allocate({UsmKind::Shared, bytes, 0, device_index}). */
                void* malloc_shared_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().allocate({UsmKind::Device, bytes, 0, device_index}). */
                void* malloc_device_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().allocate({UsmKind::Host, bytes, 0, device_index}). */
                void* malloc_host_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().release(ptr). */
                void free_usm(void* ptr);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Host, bytes, alignment,
                 *        device_index}).
                 */
                void* aligned_malloc_host_on(std::size_t alignment, std::size_t bytes,
                                             std::size_t device_index);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Shared, bytes, alignment,
                 *        device_index}).
                 */
                void* aligned_malloc_shared_on(std::size_t alignment, std::size_t bytes,
                                               std::size_t device_index);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Device, bytes, alignment,
                 *        device_index}).
                 */
                void* aligned_malloc_device_on(std::size_t alignment, std::size_t bytes,
                                               std::size_t device_index);

                /** @brief Forwards to memory().adopt(address, device_index, bytes). */
                void adopt_usm(void* address, std::size_t device_index,
                               std::optional<std::size_t> bytes);

                /** @brief Forwards to memory().containing_base(ptr). */
                std::optional<void*> containing_usm_base(void* ptr) const;

                /** @brief Forwards to memory().forget(address). */
                void forget_usm(void* address) noexcept;

                /** @brief Forwards to memory().live_count(). */
                std::size_t live_usm_count() const;

                /** @brief Forwards to topology().numa_distance(numa_a, numa_b). */
                int numa_distance(int numa_a, int numa_b) const;

                /** @brief Forwards to topology().core_to_numa(). */
                const std::vector<int>& cpu_core_to_numa() const;

                /** @brief Forwards to devices().wait_all(). */
                void wait_all();

                /**
                 * @brief Allocates count elements through malloc_shared(bytes, numa_id).
                 * @tparam T Element type.
                 * @param count Number of elements.
                 * @param numa_id Target NUMA node.
                 */
                template <typename T>
                T* malloc_shared(size_t count, int numa_id = 0)
                {
                    return static_cast<T*>(malloc_shared(count * sizeof(T), numa_id));
                }

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Deployment role. Standalone is the default — the runtime
                 * is a fully offline, single-machine library unless a cluster mode
                 * is explicitly requested.
                 */
                enum class RuntimeRole { Standalone, Master, Worker };

                /**
                 * @brief Promotes this context to Master mode and wires the cluster.
                 * @param transport Connected master-side transport, one peer per worker.
                 * @param policy Offload policy; null selects LeastLoadedOffloadPolicy.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void enable_distributed_master(std::shared_ptr<Distributed::ITransport> transport,
                                               sushi_ptr<Distributed::KernelRegistry>   kernels,
                                               std::shared_ptr<Distributed::IOffloadPolicy> policy = nullptr);

                /** @brief Registers a USM buffer as distributable (Master mode). */
                Distributed::BufferHandle register_distributed_buffer(void* usm_ptr, uint64_t size_bytes);

                /** @brief Bumps a buffer's version after a local write (invalidates remote caches). */
                void bump_distributed_buffer(void* usm_ptr);

                /** @brief Unregisters a buffer; memory().release() does it for a released address. */
                void unregister_distributed_buffer(void* usm_ptr);

                /** @brief Current handle for a registered pointer (used by compile()). */
                std::optional<Distributed::BufferHandle> distributed_buffer(void* usm_ptr) const;

                /** @brief True if the cluster can run this OpID (used by compile()). */
                bool distributed_supports(Graph::OpID op) const;

                /** @brief Current deployment role. */
                RuntimeRole distributed_role() const;

                /** @brief Tasks shipped to workers so far (Master mode telemetry; 0 otherwise). */
                uint64_t distributed_offloaded_count() const;

                /** @brief Offloadable tasks the policy kept local (Master mode telemetry). */
                uint64_t distributed_declined_count() const;

                /** @brief Tasks re-injected after a worker death / remote failure (Master mode telemetry). */
                uint64_t distributed_redispatched_count() const;

                /** @brief Writebacks rejected by the version fence — stale/superseded results (Master mode telemetry). */
                uint64_t distributed_stale_writeback_count() const;

                /**
                 * @brief Sets the worker liveness timeout (Master mode).
                 *
                 * A worker silent for longer than this is declared dead and its
                 * in-flight tasks are re-dispatched to a survivor or run locally.
                 */
                void distributed_set_liveness_timeout(std::chrono::milliseconds timeout);

                /**
                 * @brief Sets the per-task offload deadline (Master mode; default 30s).
                 *
                 * A task with no result within this bound is re-dispatched, covering
                 * a lost dispatch/result to a still-heartbeating worker. 0 disables.
                 */
                void distributed_set_task_deadline(std::chrono::milliseconds deadline);

                /** @brief Number of live (registered) workers in the cluster. */
                int distributed_alive_workers() const;
#endif

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
