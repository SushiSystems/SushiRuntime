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

namespace SushiRuntime
{
    namespace Graph { class OffloadCoordinator; }

    namespace Execution
    {
        /**
         * @brief Builds the topology view, the device queues and the USM allocator, and owns
         *        the execution engine that runs on them.
         *
         * Constructed once per process and shared (via shared_ptr) between the fluent
         * API layer and all graphs it produces. Uses Pimpl to keep the ABI stable.
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

                /**
                 * @brief Installs the coordinator that may run this context's graph nodes
                 *        elsewhere, and shares its ownership.
                 * @pre No run is in flight.
                 * @param offload Null removes the installed one. The context releases its share
                 *        when another is installed and, in its destructor, after the engine.
                 * @see include/SushiRuntime/execution/README.md
                 */
                void install_offload(std::shared_ptr<Graph::OffloadCoordinator> offload);

                /** @brief Returns the installed offload coordinator, or nullptr for none. */
                [[nodiscard]] Graph::OffloadCoordinator* offload() const noexcept;

                // Every member from here to the template malloc_shared forwards to a part above and is deprecated.

                /** @brief Returns settings().profiling. */
                [[deprecated("use settings().profiling")]]
                bool profiling_enabled() const noexcept;

                /** @brief Forwards to transfers().add_device_to_host(bytes). */
                [[deprecated("use transfers().add_device_to_host()")]]
                void add_device_to_host_bytes(std::size_t bytes) noexcept;

                /** @brief Forwards to transfers().add_host_to_device(bytes). */
                [[deprecated("use transfers().add_host_to_device()")]]
                void add_host_to_device_bytes(std::size_t bytes) noexcept;

                /** @brief Forwards to transfers().device_to_host(). */
                [[deprecated("use transfers().device_to_host()")]]
                std::size_t device_to_host_bytes() const noexcept;

                /** @brief Forwards to transfers().host_to_device(). */
                [[deprecated("use transfers().host_to_device()")]]
                std::size_t host_to_device_bytes() const noexcept;

                /**
                 * @brief Forwards to devices().queue(xpu_index).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                [[deprecated("use devices().queue()")]]
                sycl::queue& get_queue(size_t xpu_index = 0);

                /**
                 * @brief Forwards to devices().queue(xpu_index, {false, true}).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 * @see include/SushiRuntime/execution/README.md
                 */
                [[deprecated("use devices().queue() with a profiling QueueFlavour")]]
                sycl::queue& profiling_queue(size_t xpu_index = 0);

                /** @brief Forwards to devices().has_profiling_queue(xpu_index). */
                [[deprecated("use devices().has_profiling_queue()")]]
                bool has_profiling_queue(size_t xpu_index) const noexcept;

                /**
                 * @brief Forwards to devices().queue(xpu_index, {true, profiling}).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 * @see include/SushiRuntime/execution/README.md
                 */
                [[deprecated("use devices().queue() with an in-order QueueFlavour")]]
                sycl::queue& chain_queue(size_t xpu_index, bool profiling);

                /** @brief Forwards to devices().count(). */
                [[deprecated("use devices().count()")]]
                size_t device_count() const;

                /**
                 * @brief Forwards to devices().is_gpu(xpu_index).
                 * @throws Core::SushiException With Errc::invalid_access on a bad index.
                 */
                [[deprecated("use devices().is_gpu()")]]
                bool device_is_gpu(size_t xpu_index) const;

                /** @brief Forwards to devices().queue_or_numa_local(std::nullopt, numa_id). */
                [[deprecated("use devices().queue_or_numa_local()")]]
                sycl::queue& get_numa_local_queue(int numa_id);

                /**
                 * @brief Forwards to devices().queue_or_numa_local(memory().owning_device(ptr),
                 *        fallback_numa).
                 * @see include/SushiRuntime/execution/README.md
                 */
                [[deprecated("use devices().queue_or_numa_local() with memory().owning_device()")]]
                sycl::queue& get_queue_for_ptr(void* ptr, int fallback_numa = 0);

                /**
                 * @brief Forwards to memory().device_of(ptr).
                 * @return The device index, or -1 where device_of() returns nothing.
                 */
                [[deprecated("use memory().device_of()")]]
                int context_index_for_ptr(void* ptr) noexcept;

                /**
                 * @brief Forwards to memory().allocate() with UsmKind::Shared on
                 *        devices().device_for_numa(numa_id).
                 * @see include/SushiRuntime/execution/README.md
                 */
                [[deprecated("use memory().allocate() with devices().device_for_numa()")]]
                void* malloc_shared(size_t bytes, int numa_id = 0);

                /**
                 * @brief Forwards to memory().allocate() with UsmKind::Device on
                 *        devices().device_for_numa(numa_id).
                 */
                [[deprecated("use memory().allocate() with devices().device_for_numa()")]]
                void* malloc_device(size_t bytes, int numa_id = 0);

                /** @brief Forwards to memory().allocate({UsmKind::Shared, bytes, 0, device_index}). */
                [[deprecated("use memory().allocate()")]]
                void* malloc_shared_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().allocate({UsmKind::Device, bytes, 0, device_index}). */
                [[deprecated("use memory().allocate()")]]
                void* malloc_device_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().allocate({UsmKind::Host, bytes, 0, device_index}). */
                [[deprecated("use memory().allocate()")]]
                void* malloc_host_on(size_t bytes, size_t device_index);

                /** @brief Forwards to memory().release(ptr). */
                [[deprecated("use memory().release()")]]
                void free_usm(void* ptr);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Host, bytes, alignment,
                 *        device_index}).
                 */
                [[deprecated("use memory().allocate()")]]
                void* aligned_malloc_host_on(std::size_t alignment, std::size_t bytes,
                                             std::size_t device_index);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Shared, bytes, alignment,
                 *        device_index}).
                 */
                [[deprecated("use memory().allocate()")]]
                void* aligned_malloc_shared_on(std::size_t alignment, std::size_t bytes,
                                               std::size_t device_index);

                /**
                 * @brief Forwards to memory().allocate({UsmKind::Device, bytes, alignment,
                 *        device_index}).
                 */
                [[deprecated("use memory().allocate()")]]
                void* aligned_malloc_device_on(std::size_t alignment, std::size_t bytes,
                                               std::size_t device_index);

                /** @brief Forwards to memory().adopt(address, device_index, bytes). */
                [[deprecated("use memory().adopt()")]]
                void adopt_usm(void* address, std::size_t device_index,
                               std::optional<std::size_t> bytes);

                /** @brief Forwards to memory().containing_base(ptr). */
                [[deprecated("use memory().containing_base()")]]
                std::optional<void*> containing_usm_base(void* ptr) const;

                /** @brief Forwards to memory().forget(address). */
                [[deprecated("use memory().forget()")]]
                void forget_usm(void* address) noexcept;

                /** @brief Forwards to memory().live_count(). */
                [[deprecated("use memory().live_count()")]]
                std::size_t live_usm_count() const;

                /** @brief Forwards to topology().numa_distance(numa_a, numa_b). */
                [[deprecated("use topology().numa_distance()")]]
                int numa_distance(int numa_a, int numa_b) const;

                /** @brief Forwards to topology().core_to_numa(). */
                [[deprecated("use topology().core_to_numa()")]]
                const std::vector<int>& cpu_core_to_numa() const;

                /** @brief Forwards to devices().wait_all(). */
                [[deprecated("use devices().wait_all()")]]
                void wait_all();

                /**
                 * @brief Allocates count elements through malloc_shared(bytes, numa_id).
                 * @tparam T Element type.
                 * @param count Number of elements.
                 * @param numa_id Target NUMA node.
                 */
                template <typename T>
                [[deprecated("use memory().allocate() with devices().device_for_numa()")]]
                T* malloc_shared(size_t count, int numa_id = 0)
                {
                    return static_cast<T*>(malloc_shared(count * sizeof(T), numa_id));
                }

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
