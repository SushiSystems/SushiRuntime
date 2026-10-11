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

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

namespace SushiRuntime 
{
    namespace Internal::Graph
    {
        struct Node;

        /**
         * @brief Records task nodes, orders them by the regions they touch and compiles them
         *        into a CompiledGraph; it runs nothing.
         * @see modules/engine/graph/README.md
         */
        class SUSHIRUNTIME_API TaskGraph 
        {
            public:
                /** @brief Stands for "no node carries an index yet". */
                static constexpr std::uint32_t K_NO_REGISTRATION = static_cast<std::uint32_t>(-1);

                /** @brief Creates an empty task graph. */
                TaskGraph();

                /** @brief Destroys the graph and cleans up task states. */
                ~TaskGraph();

                /** @brief Disable copy for Pimpl safety and state integrity. */
                TaskGraph(const TaskGraph&) = delete;
                TaskGraph& operator=(const TaskGraph&) = delete;

                /**
                 * @brief Adds a device task whose work receives a sycl::handler.
                 * @param registration Index to stamp on this node; none draws a fresh one.
                 * @pre registration, when given, is an index reserve_registration() returned here.
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work, const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task ordered by the USM regions it touches.
                 * @param registration Index to stamp on this node; see the first add_node().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work,
                               const std::vector<BufferRegion>& read_access,
                               const std::vector<BufferRegion>& write_access,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a device task carrying a late-binding block.
                 * @param bindings Shared per-step controls; null behaves like the plain overload.
                 * @param registration Index to stamp on this node; see the first add_node().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_node(TaskWork work,
                               const std::vector<BufferRegion>& read_access,
                               const std::vector<BufferRegion>& write_access,
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
                 * @brief Returns the node most recently added, for a caller reading its state.
                 * @return The node, or nullptr while the graph holds no node.
                 */
                const Node* last_node_added() const noexcept;

                /**
                 * @brief Returns the node most recently added, for a caller stamping it.
                 * @return The node, or nullptr while the graph holds no node.
                 * @pre The caller is the thread that added the node, before compile().
                 */
                Node* last_node_added() noexcept;

                /**
                 * @brief Draws a fresh registration index from this graph's counter.
                 * @return An index any number of later add calls on this graph may be given.
                 * @see modules/engine/graph/README.md
                 */
                std::uint32_t reserve_registration() noexcept;

                /**
                 * @brief Adds a semantic task with explicit device work and a host fallback.
                 * @param registration Index to stamp on this node; see the first add_node().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_task(const TaskMetadata& metadata,
                               const std::vector<BufferRegion>& read_access,
                               const std::vector<BufferRegion>& write_access,
                               TaskWork device_work,
                               HostWork fallback_work,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Adds a semantic task with a host-side fallback only.
                 * @param registration Index to stamp on this node; see the first add_node().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_task(const TaskMetadata& metadata,
                               const std::vector<BufferRegion>& read_access,
                               const std::vector<BufferRegion>& write_access,
                               HostWork fallback_work,
                               const std::vector<sycl::event>& dependencies = {},
                               std::optional<std::uint32_t> registration = std::nullopt);


                /**
                 * @brief Adds a pure-CPU task run natively, with no SYCL submission.
                 * @param work Callable run once on a worker thread; submits no SYCL work.
                 * @param registration Index to stamp on this node; see the first add_node().
                 * @return The node added; the graph owns it, then the plan compile() returns.
                 * @see modules/engine/graph/README.md
                 */
                Node& add_native(const std::vector<BufferRegion>& read_access,
                                 const std::vector<BufferRegion>& write_access,
                                 std::function<void()> work,
                                 const std::vector<sycl::event>& dependencies = {},
                                 std::optional<std::uint32_t> registration = std::nullopt);

                /**
                 * @brief Resolves data dependencies and produces a CompiledGraph ready to execute.
                 * @pre Every node is inserted first; not thread-safe against add_node() or
                 *        add_task().
                 * @return The plan, or an InvalidGraph error that names one node of a cycle.
                 * @see modules/engine/graph/README.md
                 */
                [[nodiscard]] Result<CompiledGraph> compile();

                /**
                 * @brief Hands off the built nodes without finalising a plan.
                 * @return The region's nodes, in insertion order; empty if none.
                 * @see modules/engine/graph/README.md
                 */
                std::vector<sushi_ptr<SushiRuntime::Internal::Graph::Node>> take_nodes();

            private:
                struct Impl;
                sushi_ptr<Impl> impl_;
        };
    
    } // namespace Graph
} // namespace SushiRuntime
