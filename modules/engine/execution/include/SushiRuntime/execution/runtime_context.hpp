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
 * @see modules/engine/execution/README.md
 */

#pragma once

#include <memory>
#include <string>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { class OffloadCoordinator; class OffloadSink; }

    namespace Internal::Execution
    {
        /**
         * @brief Builds the topology view, the device queues and the USM allocator, and owns
         *        the execution engine that runs on them.
         *
         * Constructed once per process and shared (via shared_ptr) between the fluent
         * API layer and all graphs it produces. Implemented with Pimpl.
         */
        class SUSHIRUNTIME_API RuntimeContext
        {
            public:
                /**
                 * @brief Creates a runtime context and scans for hardware matching config.
                 * @param provider Topology source; null selects the hwloc default.
                 * @param distance_model NUMA distance model; null selects the hwloc default.
                 * @throws SushiException With Errc::unsupported when no device passes config.
                 * @see modules/engine/execution/README.md
                 */
                explicit RuntimeContext(const Internal::Topology::HardwareFilterConfig& config = {},
                                        std::shared_ptr<Internal::Topology::TopologyProvider> provider = nullptr,
                                        std::shared_ptr<Internal::Topology::NUMADistanceModel> distance_model = nullptr,
                                        const RuntimeSettings& settings = {});

                /** @brief Stops the engine, then waits for every device queue. */
                ~RuntimeContext();

                /** @brief Copy and move are disabled to keep state integrity. */
                RuntimeContext(const RuntimeContext&) = delete;
                RuntimeContext& operator=(const RuntimeContext&) = delete;

                /**
                 * @brief Returns the runtime-wide settings this context was created with.
                 * @return A reference valid for this context's lifetime.
                 * @see modules/engine/execution/README.md
                 */
                const RuntimeSettings& settings() const noexcept;

                /** @brief Returns the core-to-NUMA map and the NUMA distances. */
                [[nodiscard]] const Internal::Topology::TopologyView& topology() const noexcept;

                /** @brief Returns the device inventory and every SYCL queue built on it. */
                [[nodiscard]] Internal::Device::DeviceQueueSet& devices() noexcept;

                /** @brief Returns the allocator that records and releases this context's USM. */
                [[nodiscard]] Internal::Memory::USMAllocator& memory() noexcept;

                /**
                 * @brief Returns the allocator as a shared handle, for an owner that may outlive
                 *        this context.
                 */
                [[nodiscard]] const std::shared_ptr<Internal::Memory::USMAllocator>& shared_memory() const noexcept;

                /** @brief Returns the byte counters of explicit host and device transfers. */
                [[nodiscard]] Internal::Observability::TransferCounters& transfers() noexcept;

                /**
                 * @brief Returns the execution engine owned by this context.
                 *
                 * Separates thread orchestration from DAG construction: TaskGraph
                 * builds the plan; the engine drives it through the worker pool.
                 *
                 * @return ExecutionEngine& Reference to the active engine.
                 */
                ExecutionEngine& execution_engine();

                /**
                 * @brief Explicitly shuts down the execution engine and its worker pool.
                 *
                 * Calling this before the RuntimeContext destructor is optional but
                 * allows controlled, ordered teardown in sensitive environments.
                 * The destructor will call this automatically if not done explicitly.
                 */
                void shutdown();

                /**
                 * @brief Installs the coordinator that may run this context's nodes and co-owns it.
                 * @pre No run is in flight, and every TaskGraph built with the coordinator being
                 *      replaced has been compiled, emptied by take_nodes() or destroyed.
                 * @param offload Null removes the installed one. The context releases its share
                 *        when another is installed and, in its destructor, after the engine.
                 * @see modules/engine/execution/README.md
                 */
                void install_offload(std::shared_ptr<Internal::Graph::OffloadCoordinator> offload);

                /** @brief Returns the installed offload coordinator, or nullptr for none. */
                [[nodiscard]] Internal::Graph::OffloadCoordinator* offload() const noexcept;

                /**
                 * @brief Returns the sink an offload coordinator hands its nodes back to.
                 *
                 * A coordinator may be bound to it and started before install_offload(), so a
                 * start that fails leaves the installed one in place.
                 */
                [[nodiscard]] Internal::Graph::OffloadSink& offload_sink();

            private:
                struct Impl;
                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
        };

    } // namespace Execution
} // namespace SushiRuntime
