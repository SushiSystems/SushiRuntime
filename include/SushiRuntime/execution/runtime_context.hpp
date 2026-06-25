/**************************************************************************/
/* runtime_context.hpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
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
         * @brief Central owner of hardware, SYCL queues, USM allocators, and the execution engine.
         *
         * Constructed once per process and shared (via shared_ptr) between the fluent
         * API layer and all graphs it produces. Uses Pimpl to keep the ABI stable.
         * In Standalone mode (default) carries zero distributed overhead.
         */
        class SUSHIRUNTIME_API RuntimeContext 
        {
            public:
                /**
                 * @brief Creates a runtime context and scans for hardware matching @p config.
                 *
                 * Topology discovery and NUMA distances are supplied through injected
                 * interfaces. When null, production defaults are used:
                 *   - provider       → HwlocTopologyProvider
                 *   - distance_model → HwlocDistanceModel
                 *
                 * Pass RemappingTopologyProvider / MatrixDistanceModel in tests to
                 * exercise multi-node logic on single-node hardware.
                 *
                 * @param config         Device-class filter for hardware discovery.
                 * @param provider       Topology source; null selects the hwloc default.
                 * @param distance_model NUMA distance model; null selects the hwloc default.
                 */
                explicit RuntimeContext(const Topology::HardwareFilterConfig& config = {},
                                        std::shared_ptr<Topology::ITopologyProvider> provider = nullptr,
                                        std::shared_ptr<Topology::INumaDistanceModel> distance_model = nullptr);
                
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
                 * @brief Gets a SYCL queue for a specific device.
                 * @param xpu_index The index of the device (default is 0).
                 * @return sycl::queue& Reference to the device queue.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Number of device queues exposed by get_queue().
                 *
                 * Reflects SushiRuntime's own post-dedup device set (one CPU per
                 * NUMA node, GPUs once each), which can differ from
                 * sycl::device::get_devices(): valid indices are [0, count).
                 *
                 * @return The count of addressable device queues.
                 */
                size_t device_count() const;

                /**
                 * @brief Selects a queue based on NUMA location.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sycl::queue& Reference to the closest device queue.
                 */
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief Returns the queue whose context owns @p ptr (USM data).
                 *
                 * USM is pinned to the device/context it was allocated on, so a
                 * kernel touching it must run on a queue sharing that context;
                 * running it elsewhere (e.g. CPU USM on a CUDA queue) is an
                 * illegal cross-context access. If @p ptr is null or not found in
                 * any queue's context, falls back to get_numa_local_queue(@p
                 * fallback_numa) — preserving round-robin for pointer-less tasks.
                 *
                 * @param ptr           A USM pointer the task reads or writes.
                 * @param fallback_numa NUMA hint used when @p ptr can't be located.
                 * @return The owning queue, or the NUMA-local fallback.
                 */
                sycl::queue& get_queue_for_ptr(void* ptr, int fallback_numa = 0);

                /**
                 * @brief Allocates shared USM and records its owning queue.
                 *
                 * Prefer this over a bare sycl::malloc_shared when the data will
                 * be touched by graph tasks: the runtime remembers which queue
                 * (device + context) the allocation belongs to, so the scheduler
                 * can dispatch the task to that exact queue and never trigger a
                 * cross-context illegal access. The allocation is pinned to the
                 * NUMA-local compute queue for @p numa_id.
                 *
                 * @param bytes Allocation size in bytes.
                 * @param numa_id Target NUMA node (selects the owning queue).
                 * @return USM pointer, or nullptr on failure.
                 */
                void* malloc_shared(size_t bytes, int numa_id = 0);

                /**
                 * @brief Allocates device USM and records its owning queue.
                 * @see malloc_shared
                 */
                void* malloc_device(size_t bytes, int numa_id = 0);

                /**
                 * @brief Allocates shared USM pinned to one specific device queue.
                 *
                 * Unlike malloc_shared(bytes, numa_id), which round-robins across the
                 * queues on a NUMA node, this allocates on exactly @p device_index
                 * and records that owner. Use it when several buffers must share one
                 * context — a kernel touching USM from two different device contexts
                 * is an illegal cross-context access. The fluent Buffer/State handles
                 * allocate through this so a graph's data stays co-located.
                 *
                 * @param bytes        Allocation size in bytes.
                 * @param device_index Target device queue index ([0, device_count())).
                 * @return USM pointer, or nullptr on failure.
                 */
                void* malloc_shared_on(size_t bytes, size_t device_index);

                /**
                 * @brief Allocates device-only USM pinned to one specific device queue.
                 *
                 * The device-resident counterpart of malloc_shared_on: the allocation
                 * stays on @p device_index and is never host-addressable, so a field
                 * kept resident across frames does not migrate to the host on access.
                 * The owner is recorded exactly as for shared USM, so the scheduler
                 * routes kernels to the owning queue and selective host readback copies
                 * through it. Use it when several device-resident buffers must share one
                 * context (a State's two fields).
                 *
                 * @param bytes        Allocation size in bytes.
                 * @param device_index Target device queue index ([0, device_count())).
                 * @return Device USM pointer, or nullptr on failure.
                 */
                void* malloc_device_on(size_t bytes, size_t device_index);

                /**
                 * @brief Frees USM obtained from malloc_shared/malloc_device and
                 *        drops its owner record. Safe to call with nullptr.
                 */
                void free_usm(void* ptr);

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
                 * @brief Returns the main USM allocator for this context.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The global allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator();

                /**
                 * @brief Returns a USM allocator optimized for a specific NUMA node.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The NUMA-optimized allocator.
                 */
                sushi_ptr<SushiRuntime::Memory::USMAllocator> get_allocator(int numa_id);

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
                 *
                 * Creates the buffer registry and the coordinator, installs the
                 * scheduler completion hook, and starts the transport. Until this is
                 * called the context behaves exactly as Standalone (zero overhead).
                 *
                 * @param transport Connected master-side transport (one peer per worker).
                 * @param kernels   Registry shared with this binary's kernels.
                 * @param policy    Offload policy (null → LeastLoadedOffloadPolicy).
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
        };
    
    } // namespace Execution
} // namespace SushiRuntime
