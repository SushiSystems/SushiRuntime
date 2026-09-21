/**************************************************************************/
/* task_graph.hpp                                                         */
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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/resource_region.hpp>
#include <SushiRuntime/graph/late_bindings.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime 
{
    namespace Execution 
    { 
        class RuntimeContext; 
    }

    namespace Graph
    {
        struct Node;

        /**
         * @brief Task work type defined as a SYCL kernel or transfer.
         */
        using TaskWork = std::function<void(sycl::handler&)>;

        /**
         * @brief Task work type for host-side (library) execution. 
         */
        using HostWork = std::function<sycl::event(sycl::queue&, const std::vector<sycl::event>&)>;

        /**
         * @brief Manages and optimizes dependencies between async tasks.
         * 
         * TaskGraph collects work (kernels), analyzes data dependencies,
         * and executes them in the best order on the hardware.
         */
        class SUSHIRUNTIME_API TaskGraph 
        {
            public:
                /** @brief Stands for "no node carries an index yet". */
                static constexpr std::uint32_t NO_REGISTRATION = static_cast<std::uint32_t>(-1);

                /**
                 * @brief Creates a new task graph.
                 * @param ctx Reference to the associated RuntimeContext.
                 */
                TaskGraph(SushiRuntime::Execution::RuntimeContext& ctx);
                
                /** @brief Destroys the graph and cleans up task states. */
                ~TaskGraph();

                /** @brief Disable copy for Pimpl safety and state integrity. */
                TaskGraph(const TaskGraph&) = delete;
                TaskGraph& operator=(const TaskGraph&) = delete;

                /**
                 * @brief Adds a task for the device (GPU/CPU).
                 * 
                 * Use this for your own SYCL kernels or memory transfers.
                 * It gives you a sycl::handler to define the work.
                 * 
                 * @param work Function that uses a sycl::handler to run work.
                 * @param dependencies Manual events to wait for before starting.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {},
                             std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task with automatic dependency tracking.
                 *
                 * The runtime orders this task by the USM regions it touches: two
                 * tasks are serialized only when their regions overlap, so disjoint
                 * sub-ranges of one allocation run in parallel. A bare base pointer
                 * converts to the whole-buffer region, the conservative default.
                 *
                 * @param work Function that uses a sycl::handler to run work.
                 * @param read_access Regions this task reads from.
                 * @param write_access Regions this task writes to.
                 * @param dependencies Additional manual events to wait for.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_node(TaskWork work,
                             const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                             const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                             const std::vector<sycl::event>& dependencies = {},
                             std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task carrying a late-binding block.
                 *
                 * The dependency-tracked overload above plus a per-step control
                 * block: the same region ordering applies, but the node also holds
                 * @p bindings so the scheduler can skip it when disabled this step
                 * and the kernel can read its live iteration size. The compiled
                 * topology stays fixed; only size and the enable flag vary per step,
                 * so the graph is built once and replayed without recompiling.
                 *
                 * @param work Function that uses a sycl::handler to run work.
                 * @param read_access Regions this task reads from.
                 * @param write_access Regions this task writes to.
                 * @param bindings Shared per-step controls; null behaves like the plain overload.
                 * @param dependencies Additional manual events to wait for.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_node(TaskWork work,
                             const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                             const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                             std::shared_ptr<LateBindings> bindings,
                             const std::vector<sycl::event>& dependencies = {},
                             std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Names the node most recently added to this graph.
                 *
                 * The name reaches the profiler as RunReport::NodeTiming::name. It is
                 * set after the fact rather than passed to each add_* overload because
                 * two of the fluent launch shapes end in a parameter pack whose last
                 * element is the callable, so a trailing name is not expressible there.
                 *
                 * Called during graph description, on the thread that just added the
                 * node, so it needs no synchronisation of its own.
                 *
                 * @param name Persistent label, stored not copied; a string literal is
                 *             the intended use. Null, or no node yet added, is a no-op.
                 */
                void name_last_node(const char* name) noexcept;

                /**
                 * @brief Stamps the element count on the node most recently added.
                 * @param width Elements the node's dispatch launches over.
                 * @pre Compilation is single-threaded, so "most recently added" names
                 *      one node; a parallel compiler must stamp width at the add call.
                 */
                void width_last_node(std::size_t width) noexcept;

                /**
                 * @brief Returns the element count stamped on the last node added.
                 * @return The width, or Core::WIDTH_UNKNOWN while the graph holds no node.
                 */
                std::size_t last_node_width() const noexcept;

                /**
                 * @brief Returns the node most recently added, for a caller reading its state.
                 * @return The node, or nullptr while the graph holds no node.
                 */
                const Node* last_node_added() const noexcept;

                /**
                 * @brief Draws a fresh registration index from this graph's counter.
                 *
                 * Lets an add() shape that registers several internal nodes for one
                 * public call (a reduction's tile levels) reserve one index up front
                 * and pass it to each node's add_* call as @c registration, so they
                 * report the same Node::registration_index. Safe to call any number
                 * of times; each call returns a distinct value.
                 *
                 * @return The reserved registration index.
                 */
                std::uint32_t reserve_registration() noexcept;

                /**
                 * @brief Returns the registration index stamped on the last node added.
                 *
                 * Reads the same node name_last_node() names, so a builder that
                 * replays one recorded rule can ask which index that rule took.
                 *
                 * @return The index, or @ref NO_REGISTRATION while the graph holds no node.
                 */
                std::uint32_t last_registration_index() const noexcept;

                /**
                 * @brief Adds a semantic task into the graph with explicit device and host fallback work.
                 * 
                 * Use this overload for kernel-capable tasks that need a real device lambda.
                 * The host fallback is still tracked for CPU-only or library execution paths.
                 * 
                 * @param meta Definitions containing the operation type and properties.
                 * @param read_access Memory pointers that this operation reads.
                 * @param write_access Memory pointers that this operation overwrites or updates.
                 * @param device_work The actual SYCL handler body used in kernel mode.
                 * @param fallback_work A lambda executed on the host queue when host execution is selected.
                 * @param dependencies Additional explicit user dependencies.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_task(const TaskMetadata& meta,
                              const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                              const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                              TaskWork device_work,
                              HostWork fallback_work,
                              const std::vector<sycl::event>& dependencies = {},
                              std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a semantic task into the graph with host-side fallback only.
                 * 
                 * This is the compatibility overload for host-executed semantic tasks.
                 * Kernel-capable tasks should prefer the overload that also receives device_work.
                 * 
                 * @param meta Definitions containing the operation type and properties.
                 * @param read_access Memory pointers that this operation reads.
                 * @param write_access Memory pointers that this operation overwrites or updates.
                 * @param fallback_work A lambda function executed safely on host execution.
                 * @param dependencies Additional explicit user dependencies.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_task(const TaskMetadata& meta,
                              const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                              const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                              HostWork fallback_work,
                              const std::vector<sycl::event>& dependencies = {},
                              std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds an offloadable task whose buffers rotate every step.
                 *
                 * The double-buffered counterpart to the offload add_task overloads:
                 * for a task that reads one allocation and writes another and swaps
                 * the two each step (a distributed State), the touched buffers cannot
                 * be frozen at compile time. The node carries the shared @p live_reads
                 * / @p live_writes pointer lists, which the caller re-points before
                 * each step; compile() marks the node offloadable without baking
                 * handles, and the coordinator resolves them from the live lists at
                 * dispatch. Dependency ordering and queue routing use the lists'
                 * current contents at insertion.
                 *
                 * @param meta        Metadata carrying the offloadable op_id.
                 * @param live_reads  Shared list of the buffers read this step.
                 * @param live_writes Shared list of the buffers written this step.
                 * @param fallback_work Host fallback run when the task is not offloaded.
                 * @param dependencies Additional explicit user dependencies.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_offload_dynamic(const TaskMetadata& meta,
                                         std::shared_ptr<std::vector<void*>> live_reads,
                                         std::shared_ptr<std::vector<void*>> live_writes,
                                         HostWork fallback_work,
                                         const std::vector<sycl::event>& dependencies = {},
                                         std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a pure-CPU task run natively, with no SYCL submission.
                 *
                 * For host work that touches no device API — gameplay logic, AI,
                 * navigation, the CPU half of a broadphase. The node is ordered by
                 * the regions it reads/writes exactly like a kernel task, but the
                 * scheduler runs @p work directly on a worker thread and reports an
                 * empty event, so completion uses the same successor-release path as
                 * any other node. By the time the node is dispatched its predecessor
                 * events have completed, so the shared USM it reads is visible.
                 *
                 * @param read_access Regions this task reads from.
                 * @param write_access Regions this task writes to.
                 * @param work Callable run once on a worker thread; submits no SYCL work.
                 * @param dependencies Additional manual events to wait for.
                 * @param registration Registration index to stamp on this node instead
                 *                     of drawing a fresh one from reserve_registration().
                 */
                void add_native(const std::vector<SushiRuntime::Core::ResourceRegion>& read_access,
                                const std::vector<SushiRuntime::Core::ResourceRegion>& write_access,
                                std::function<void()> work,
                                const std::vector<sycl::event>& dependencies = {},
                                std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Resolves data dependencies and produces a CompiledGraph ready to execute.
                 *
                 * Links leaf nodes to a sentinel completion node, identifies root nodes
                 * (zero in-degree), and transfers node ownership to the returned
                 * CompiledGraph. Does not submit any work to hardware.
                 *
                 * @warning Not thread-safe with respect to add_node() / add_task().
                 *          All nodes must be inserted before compile() is called.
                 *
                 * @return CompiledGraph Compiled plan; call execute() on it to run.
                 */
                SushiRuntime::Execution::CompiledGraph compile();

                /**
                 * @brief Hands off the built nodes without finalising a plan.
                 *
                 * Drains the insertion stacks and returns the node objects exactly
                 * as inserted, without resolving roots/leaves or wiring a sentinel.
                 * The dynamic region graph builds a region's nodes once with this to
                 * obtain a sub-graph whose intra-region edges are already baked (the
                 * dependency pass ran during add_*), then caches it and stitches only
                 * the cross-region boundary on top — so a region that did not change
                 * is never rebuilt. The returned nodes keep their intra edges and
                 * carry their in_degree as the region's own pass left it.
                 *
                 * @return The region's nodes, in insertion order. Empty if none.
                 */
                std::vector<sushi_ptr<SushiRuntime::Graph::Node>> take_nodes();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
