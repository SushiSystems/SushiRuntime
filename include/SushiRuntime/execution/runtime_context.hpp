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
 * @brief Declares RuntimeContext, the owner of hardware, queues, USM and the execution engine.
 * @author Mustafa Garip
 * @see include/SushiRuntime/execution/README.md
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_registry.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#ifdef SR_DISTRIBUTED
#include <chrono>
#include <optional>
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
         * @brief Central owner of hardware, SYCL queues, USM ownership, and the execution engine.
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
                 * @see include/SushiRuntime/execution/README.md
                 */
                explicit RuntimeContext(const Topology::HardwareFilterConfig& config = {},
                                        std::shared_ptr<Topology::ITopologyProvider> provider = nullptr,
                                        std::shared_ptr<Topology::INumaDistanceModel> distance_model = nullptr,
                                        const RuntimeSettings& settings = {});
                
                /** @brief Destroys the context and frees all resources. */
                ~RuntimeContext();

                /** @brief Copy and move are disabled to keep state integrity. */
                RuntimeContext(const RuntimeContext&) = delete;
                RuntimeContext& operator=(const RuntimeContext&) = delete;

                /**
                 * @brief Re-scans hardware and rebuilds the device queue list.
                 *
                 * Normally called automatically by the constructor. Call explicitly
                 * only when the hardware topology changes at runtime (rare).
                 *
                 * @param config Device-class filter; defaults to including all classes.
                 */
                void probe_hardware(const Topology::HardwareFilterConfig& config = {});

                /**
                 * @brief Returns whether this context's queues were built with profiling on.
                 * @see include/SushiRuntime/execution/README.md
                 */
                bool profiling_enabled() const noexcept;

                /**
                 * @brief Returns the runtime-wide settings this context was created with.
                 * @return A reference valid for this context's lifetime.
                 * @see include/SushiRuntime/execution/README.md
                 */
                const RuntimeSettings& settings() const noexcept;

                /**
                 * @brief Records bytes copied from device to host by an explicit transfer.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void add_device_to_host_bytes(std::size_t bytes) noexcept;

                /**
                 * @brief Records bytes copied host->device by an explicit transfer.
                 * @param bytes Number of bytes copied to the device (Buffer::write_range).
                 */
                void add_host_to_device_bytes(std::size_t bytes) noexcept;

                /** @brief Cumulative device->host bytes from explicit transfers. */
                std::size_t device_to_host_bytes() const noexcept;

                /** @brief Cumulative host->device bytes from explicit transfers. */
                std::size_t host_to_device_bytes() const noexcept;

                /**
                 * @brief Gets a SYCL queue for a specific device.
                 * @param xpu_index The index of the device (default is 0).
                 * @return sycl::queue& Reference to the device queue.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Returns the profiling-enabled queue of a device, building it on first use.
                 * @throws Core::SushiException If the index is out of bounds.
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& profiling_queue(size_t xpu_index = 0);

                /**
                 * @brief Returns whether a device's profiling queue already exists, without
                 *        building it.
                 * @see include/SushiRuntime/execution/README.md
                 */
                bool has_profiling_queue(size_t xpu_index) const noexcept;

                /**
                 * @brief Returns the in-order queue a chainable plan submits to on one device.
                 * @param profiling True for the flavour that also carries enable_profiling.
                 * @throws Core::SushiException If the index is out of bounds.
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& chain_queue(size_t xpu_index, bool profiling);

                /**
                 * @brief Returns the number of device queues exposed by get_queue().
                 * @see include/SushiRuntime/execution/README.md
                 */
                size_t device_count() const;

                /**
                 * @brief Returns whether the device behind a queue is a GPU, read from the
                 *        inventory.
                 * @param xpu_index The index of the device; valid range [0, device_count()).
                 * @throws Core::SushiException If the index is out of bounds.
                 * @see include/SushiRuntime/execution/README.md
                 */
                bool device_is_gpu(size_t xpu_index) const;

                /**
                 * @brief Selects a queue based on NUMA location.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sycl::queue& Reference to the closest device queue.
                 */
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief Returns the queue whose context owns ptr, or the NUMA-local fallback.
                 * @param fallback_numa NUMA hint used when ptr cannot be located.
                 * @see include/SushiRuntime/execution/README.md
                 */
                sycl::queue& get_queue_for_ptr(void* ptr, int fallback_numa = 0);

                /**
                 * @brief Returns the device-queue index whose context owns ptr.
                 * @return The owning device-queue index, or -1 if ptr is unknown.
                 * @see include/SushiRuntime/execution/README.md
                 */
                int context_index_for_ptr(void* ptr) noexcept;

                /**
                 * @brief Allocates shared USM and records its owning queue.
                 * @param numa_id Target NUMA node; selects the owning queue.
                 * @return USM pointer, or nullptr on failure.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void* malloc_shared(size_t bytes, int numa_id = 0);

                /**
                 * @brief Allocates device USM and records its owning queue.
                 * @see malloc_shared
                 */
                void* malloc_device(size_t bytes, int numa_id = 0);

                /**
                 * @brief Allocates shared USM pinned to one specific device queue.
                 * @param device_index Target device queue index, in [0, device_count()).
                 * @return USM pointer, or nullptr on failure.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void* malloc_shared_on(size_t bytes, size_t device_index);

                /**
                 * @brief Allocates device-only USM pinned to one specific device queue.
                 * @param device_index Target device queue index, in [0, device_count()).
                 * @return Device USM pointer, or nullptr on failure.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void* malloc_device_on(size_t bytes, size_t device_index);

                /**
                 * @brief Allocates pinned host USM in one specific device queue's context.
                 * @param device_index Target device queue index, in [0, device_count()).
                 * @return Host USM pointer, or nullptr on failure.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void* malloc_host_on(size_t bytes, size_t device_index);

                /**
                 * @brief Frees shared, device or host USM from this context's malloc_* calls
                 *        and drops its owner record. Safe to call with nullptr.
                 */
                void free_usm(void* ptr);

                /**
                 * @brief Allocates host USM aligned to @p alignment on one device queue.
                 * @param alignment    Byte alignment of the returned address.
                 * @param bytes        Allocation size in bytes.
                 * @param device_index Target device queue; selects the owning context.
                 * @return Host-addressable USM pointer, or nullptr on failure.
                 */
                void* aligned_malloc_host_on(std::size_t alignment, std::size_t bytes,
                                             std::size_t device_index);

                /**
                 * @brief Records an externally-allocated address as owned by one device queue.
                 * @param address      The address to record; not allocated by this call.
                 * @param device_index Target device queue that owns @p address.
                 * @param bytes        Length of the mapping, or nothing when the importer
                 *                     cannot know it; without a length only @p address
                 *                     itself resolves, never an offset into it.
                 */
                void adopt_usm(void* address, std::size_t device_index,
                               std::optional<std::size_t> bytes);

                /**
                 * @brief Finds the start of the recorded allocation an address falls in.
                 * @param ptr Any address, interior or base; null resolves to nothing.
                 * @return The allocation's first address, or nothing when no recorded allocation
                 *        covers ptr.
                 * @see include/SushiRuntime/execution/README.md
                 */
                std::optional<void*> containing_usm_base(void* ptr) const;

                /**
                 * @brief Drops @p address's owner record without freeing it.
                 * @param address The address to forget; safe to call with nullptr.
                 */
                void forget_usm(void* address) noexcept;

                /**
                 * @brief Counts the USM allocations this context has recorded and not yet freed.
                 * @return Live records: every malloc_* or adopt_usm not yet matched by free_usm
                 *         or forget_usm.
                 */
                std::size_t live_usm_count() const;

                /**
                 * @brief Typed convenience wrapper over malloc_shared.
                 * @tparam T Element type.
                 * @param count Number of elements.
                 * @param numa_id Target NUMA node.
                 */
                template <typename T>
                T* malloc_shared(size_t count, int numa_id = 0)
                {
                    return static_cast<T*>(malloc_shared(count * sizeof(T), numa_id));
                }

                /**
                 * @brief NUMA latency cost between two nodes, via the injected model.
                 * @return 10 for local, > 10 for remote (ACPI SLIT convention).
                 */
                int numa_distance(int numa_a, int numa_b) const;

                /**
                 * @brief Logical CPU (core) to NUMA node map, from the topology provider.
                 *
                 * Index = logical core id (as used by thread pinning). Used by the
                 * scheduler to build NUMA-aware steal victim orders. May be empty if
                 * the provider exposes no CPU topology → callers fall back to a flat
                 * single-tier policy.
                 */
                const std::vector<int>& cpu_core_to_numa() const;


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

                /**
                 * @brief Waits for all async operations to finish (global sync).
                 */
                void wait_all();

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

                /** @brief Unregisters a buffer, used internally by free_usm. */
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

                /**
                 * @brief Builds device @p xpu_index's profiling queue, once.
                 * @param xpu_index The device whose profiling queue to build.
                 * @pre xpu_index < device_count().
                 */
                void build_profiling_queue(size_t xpu_index);

                /**
                 * @brief Builds device @p xpu_index's in-order chain queue of one flavour, once.
                 * @param xpu_index The device whose chain queue to build.
                 * @param profiling True for the flavour that carries enable_profiling.
                 * @pre xpu_index < device_count().
                 */
                void build_chain_queue(size_t xpu_index, bool profiling);
        };
    
    } // namespace Execution
} // namespace SushiRuntime
