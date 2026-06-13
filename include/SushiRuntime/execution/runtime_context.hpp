/**************************************************************************/
/* runtime_context.hpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
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
#ifdef SUSHI_DISTRIBUTED
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
         * @brief Main control unit of SushiRuntime.
         * 
         * This class manages hardware, USM pools, and SYCL queues.
         * It uses the Pimpl pattern to keep the ABI stable and hide logic.
         */
        class SUSHIRUNTIME_API RuntimeContext 
        {
            public:
                /**
                 * @brief Creates a new runtime context and scans for hardware matching the config.
                 *
                 * Topology discovery and NUMA distances are injected behind interfaces
                 * [DIP]. When left null, production defaults are used:
                 *   - provider       → Topology::HwlocTopologyProvider
                 *   - distance_model → Topology::HwlocDistanceModel
                 *
                 * Tests inject RemappingTopologyProvider / MatrixDistanceModel to
                 * exercise multi-node behaviour on single-node hardware.
                 *
                 * @param config         Device-class filter for discovery.
                 * @param provider       Topology source (null → Hwloc default).
                 * @param distance_model NUMA distance source (null → Hwloc default).
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
                 * @brief Finds and lists all available hardware (CPU, GPU, FPGA).
                 */
                void probe_hardware(const Topology::HardwareFilterConfig& config = {});

                /**
                 * @brief Gets a SYCL queue for a specific device.
                 * @param xpu_index The index of the device (default is 0).
                 * @return sycl::queue& Reference to the device queue.
                 */
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Selects a queue based on NUMA location.
                 * @param numa_id The ID of the target NUMA node.
                 * @return sycl::queue& Reference to the closest device queue.
                 */
                sycl::queue& get_numa_local_queue(int numa_id);

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
                 * TaskGraph::execute() delegates to this engine so that thread
                 * orchestration is separated from DAG construction. [Madde 26]
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

#ifdef SUSHI_DISTRIBUTED
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

                /** @brief Current handle for a registered pointer (used by compile()). */
                std::optional<Distributed::BufferHandle> distributed_buffer(void* usm_ptr) const;

                /** @brief True if the cluster can run this OpID (used by compile()). */
                bool distributed_supports(Graph::OpID op) const;

                /** @brief Current deployment role. */
                RuntimeRole distributed_role() const;
#endif

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_; 
        };
    
    } // namespace Execution
} // namespace SushiRuntime
