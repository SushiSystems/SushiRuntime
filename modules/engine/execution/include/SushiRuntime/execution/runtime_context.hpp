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

#include <cstdint>
#include <memory>
#include <string>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>

namespace SushiRuntime
{
    class PlacementRules;
    struct NodeFacts;

    namespace Internal::Graph { class OffloadCoordinator; class OffloadSink; }

    namespace Internal::Execution
    {
        /** @brief Answers whether an observer wants device timing for a run armed now; must not throw. */
        using TimingGate = bool (*)() noexcept;

        /**
         * @brief Builds the topology view, the device queues and the USM allocator, and owns
         *        the execution engine that runs on them.
         *
         * Created once per process and shared (via shared_ptr) between the fluent
         * API layer and all graphs it produces. Implemented with Pimpl.
         */
        class SUSHIRUNTIME_API RuntimeContext
        {
            public:
                /**
                 * @brief Holds the rules in force, whether a coordinator is installed, and the
                 *        generation both were published under; never changed once published.
                 * @see modules/engine/execution/README.md
                 */
                struct PlacementSnapshot
                {
                    std::shared_ptr<const PlacementRules> rules; /**< Null for none. */
                    std::uint64_t generation = 0;                /**< Changes before it. */
                    bool offload_installed = false;              /**< A coordinator is in. */
                };

                /**
                 * @brief Creates a runtime context and scans for hardware matching config.
                 * @param provider Topology source; null selects the hwloc default.
                 * @param distance_model NUMA distance model; null selects the hwloc default.
                 * @return The context, or Unsupported when no device passes config or cannot be listed,
                 *         DeviceLost when a queue cannot be built, AllocationFailed when memory or a thread is refused.
                 * @see modules/engine/execution/README.md
                 */
                [[nodiscard]] static Result<std::shared_ptr<RuntimeContext>>
                create(const DeviceFilter& config = {},
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
                 * @brief Installs the coordinator that may run this context's nodes, co-owns it
                 *        and counts the change in placement().
                 * @pre No run is in flight, and every TaskGraph built with the coordinator being
                 *      replaced has been compiled, emptied by take_nodes() or destroyed.
                 * @param offload Null removes the installed one; the context releases a share
                 *        on the next install and, in its destructor, after the engine.
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

                /**
                 * @brief Adds an observer that receives every finished run; a run is timed while one is added.
                 * @param observer Not null; the context co-owns it until it is removed.
                 * @param gate Whether it wants timing now; null for always. A closed gate skips its deliveries.
                 * @return The id remove_run_observer() takes.
                 * @see modules/engine/execution/README.md
                 */
                std::uint64_t add_run_observer(std::shared_ptr<RunObserver> observer,
                                               TimingGate gate = nullptr);

                /**
                 * @brief Removes the observer an add_run_observer() call returned @p id for.
                 *
                 * An id that is not added changes nothing.
                 */
                void remove_run_observer(std::uint64_t id) noexcept;

                /** @brief Returns whether a run armed now is timed: an ungated observer is added or a gate is open; any thread. */
                [[nodiscard]] bool wants_device_timing() const noexcept;

                /** @brief Hands @p observation to every added observer, in the order they were added; any thread. */
                void notify_run(const RunObservation& observation) const noexcept;

                /**
                 * @brief Installs the placement rules the next compile reads and counts the change.
                 * @param rules Null removes the rules. A plan compiled earlier keeps its own share.
                 * @return False, changing nothing, when a run is in flight; a run that starts
                 *         meanwhile waits for the swap and then reads the new rules.
                 * @see modules/engine/execution/README.md
                 */
                [[nodiscard]] bool set_placement_rules(std::shared_ptr<const PlacementRules> rules);

                /**
                 * @brief Returns the record of the rules in force, whether a coordinator is
                 *        installed and their generation; never null, any thread.
                 *
                 * A compile reads it once; a plan compiled under another generation is stale.
                 * @see modules/engine/execution/README.md
                 */
                [[nodiscard]] std::shared_ptr<const PlacementSnapshot> placement() const noexcept;

                /**
                 * @brief Keeps the facts of the machine this context runs on.
                 * @pre Called at most once, before any graph of this context is compiled.
                 * @see modules/engine/execution/README.md
                 */
                void set_machine_facts(std::shared_ptr<const NodeFacts> facts);

                /** @brief Returns the facts of this machine, or null when none were set. */
                [[nodiscard]] const NodeFacts* machine_facts() const noexcept;

            private:
                struct Impl;

                /** @brief Takes the parts create() built; the engine is attached after it. */
                explicit RuntimeContext(sushi_ptr<Impl> impl) noexcept;

                /** @brief Pimpl handle for internal implementation details. */
                sushi_ptr<Impl> impl_;
        };

        /**
         * @brief Adds @p observer, behind @p gate when it has one, and returns the handle that removes it.
         *
         * The handle holds the context weakly, so a handle that outlives the context removes nothing.
         * @pre observer is not null.
         * @see modules/engine/execution/README.md
         */
        [[nodiscard]] SUSHIRUNTIME_API RunObserverHandle
        observe_runs(const std::shared_ptr<RuntimeContext>& context, std::shared_ptr<RunObserver> observer,
                     TimingGate gate = nullptr);

    } // namespace Execution
} // namespace SushiRuntime
