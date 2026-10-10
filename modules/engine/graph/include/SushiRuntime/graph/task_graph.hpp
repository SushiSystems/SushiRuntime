/**************************************************************************/
/* task_graph.hpp                                                         */
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
 * @file task_graph.hpp
 * @brief Declares TaskGraph, the builder that records nodes and compiles them.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

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
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime 
{
    namespace Execution 
    { 
        class RuntimeContext; 
    }

    namespace Graph
    {
        struct Node;
        class OffloadCoordinator;

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
                 * @brief Creates an empty task graph.
                 * @param offload Given the compiled nodes by compile(); null for a graph that
                 *        runs only on the local pool. Not owned; outlives the graph.
                 */
                explicit TaskGraph(OffloadCoordinator* offload = nullptr);

                /**
                 * @brief Creates an empty task graph with no offload coordinator.
                 * @param context Not read; a graph that offloads takes context.offload() instead.
                 */
                [[deprecated("TaskGraph takes no context; use TaskGraph()")]]
                explicit TaskGraph(SushiRuntime::Execution::RuntimeContext& context);


                /** @brief Destroys the graph and cleans up task states. */
                ~TaskGraph();

                /** @brief Disable copy for Pimpl safety and state integrity. */
                TaskGraph(const TaskGraph&) = delete;
                TaskGraph& operator=(const TaskGraph&) = delete;

                /**
                 * @brief Adds a device task whose work receives a sycl::handler.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task ordered by the USM regions it touches.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work,
                               const std::vector<Memory::ResourceRegion>& read_access,
                               const std::vector<Memory::ResourceRegion>& write_access,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task carrying a late-binding block.
                 * @param bindings Shared per-step controls; null behaves like the plain overload.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work,
                               const std::vector<Memory::ResourceRegion>& read_access,
                               const std::vector<Memory::ResourceRegion>& write_access,
                               std::shared_ptr<LateBindings> bindings,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Names the node most recently added to this graph.
                 * @param name Persistent label, stored not copied; null, or no node yet added, does
                 *        nothing.
                 * @see modules/engine/graph/README.md
                 */
                void name_last_node(const char* name) noexcept;

                /**
                 * @brief Stamps the element count on the node most recently added.
                 * @param width Elements the node's dispatch launches over.
                 * @pre Compilation is single-threaded, so "most recently added" names
                 *      one node; a parallel compiler must stamp width at the add call.
                 */
                [[deprecated("set baked_width on the Node& an add call returns")]]
                void width_last_node(std::size_t width) noexcept;

                /**
                 * @brief Returns the element count stamped on the last node added.
                 * @return The width, or Observability::WIDTH_UNKNOWN while the graph holds no node.
                 */
                [[deprecated("read last_node_added()->baked_width")]]
                std::size_t last_node_width() const noexcept;

                /**
                 * @brief Returns the node most recently added, for a caller reading its state.
                 * @return The node, or nullptr while the graph holds no node.
                 */
                const Node* last_node_added() const noexcept;

                /**
                 * @brief Draws a fresh registration index from this graph's counter.
                 * @see modules/engine/graph/README.md
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
                [[deprecated("read last_node_added()->registration_index")]]
                std::uint32_t last_registration_index() const noexcept;

                /**
                 * @brief Adds a semantic task with explicit device work and a host fallback.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_task(const TaskMetadata& meta,
                               const std::vector<Memory::ResourceRegion>& read_access,
                               const std::vector<Memory::ResourceRegion>& write_access,
                               TaskWork device_work,
                               HostWork fallback_work,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a semantic task with a host-side fallback only.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_task(const TaskMetadata& meta,
                               const std::vector<Memory::ResourceRegion>& read_access,
                               const std::vector<Memory::ResourceRegion>& write_access,
                               HostWork fallback_work,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds an offloadable task whose buffers rotate every step.
                 * @param live_reads Shared list of the buffers read this step.
                 * @param live_writes Shared list of the buffers written this step.
                 * @param registration Index to stamp on this node; none draws a fresh one.
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_offload_dynamic(const TaskMetadata& meta,
                                          std::shared_ptr<std::vector<void*>> live_reads,
                                          std::shared_ptr<std::vector<void*>> live_writes,
                                          HostWork fallback_work,
                                          const std::vector<sycl::event>& dependencies = {},
                                          std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a pure-CPU task run natively, with no SYCL submission.
                 * @param work Callable run once on a worker thread; submits no SYCL work.
                 * @param registration Index to stamp on this node; none draws one from
                 *        reserve_registration().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_native(const std::vector<Memory::ResourceRegion>& read_access,
                                 const std::vector<Memory::ResourceRegion>& write_access,
                                 std::function<void()> work,
                                 const std::vector<sycl::event>& dependencies = {},
                                 std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Resolves data dependencies and produces a CompiledGraph ready to execute.
                 * @pre Every node is inserted first; not thread-safe against add_node() or
                 *        add_task().
                 * @throws Core::SushiException With Errc::invalid_graph, naming one node that
                 *         cannot run, when the graph has no root or holds a cycle.
                 * @see modules/engine/graph/README.md
                 */
                CompiledGraph compile();

                /**
                 * @brief Hands off the built nodes without finalising a plan.
                 * @return The region's nodes, in insertion order; empty if none.
                 * @see modules/engine/graph/README.md
                 */
                std::vector<sushi_ptr<SushiRuntime::Graph::Node>> take_nodes();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
