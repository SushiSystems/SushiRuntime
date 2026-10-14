/**************************************************************************/
/* node_emitters.cpp                                                      */
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
 * @file node_emitters.cpp
 * @brief Implements the functions that add a described node to the engine's graph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include "node_emitters.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <variant>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/task_work.hpp>

#include <SushiRuntime/engine_handles/late_controls_access.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            using Internal::Graph::Node;
            using Internal::Graph::TaskGraph;

            static_assert(NodeOptions::K_MAX_PARAMETERS ==
                              Internal::Graph::TaskMetadata::K_MAX_PARAMS,
                          "NodeOptions carries as many parameters as the engine's node.");
            static_assert(K_NO_REGISTRATION == TaskGraph::K_NO_REGISTRATION,
                          "The surface and the engine name one value for no registration.");

            /** @brief Signature every kind's function has. */
            using Emitter = Node& (*)(TaskGraph&, const NodeDescription&,
                                      std::optional<std::uint32_t>);

            /** @brief Bakes a stated launch width on a node; K_WIDTH_UNKNOWN leaves it. */
            void bake_width(Node& node, std::size_t width) noexcept
            {
                if (width != ::SushiRuntime::K_WIDTH_UNKNOWN)
                    node.baked_width = width;
            }

            /** @brief Hands a State rule's node the live fields of a rule that has them. */
            void attach_live_fields(Node& node, const LiveFields& live)
            {
                if (live.reads != nullptr)
                    node.set_live_io(live.reads, live.writes);
            }

            /** @brief Adds a command group ordered by the regions it names. */
            Node& add_kernel(TaskGraph& backing_graph, const NodeDescription& description,
                             std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    description.reads, description.writes, {},
                                                    registration);
                bake_width(node, description.width);
                return node;
            }

            /** @brief Adds a command group that names no region and places it on its device. */
            Node& add_untracked(TaskGraph& backing_graph, const NodeDescription& description,
                                std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    std::vector<sycl::event>{}, registration);
                node.place_on(description.device);
                bake_width(node, description.width);
                return node;
            }

            /** @brief Adds a task body that submits on the queue it is handed. */
            Node& add_task(TaskGraph& backing_graph, const NodeDescription& description,
                           std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_task(Internal::Graph::TaskMetadata{},
                                                    description.reads, description.writes,
                                                    std::get<QueueWork>(description.work), {},
                                                    registration);
                bake_width(node, description.width);
                return node;
            }

            /** @brief Adds a body run once on a worker thread. */
            Node& add_host(TaskGraph& backing_graph, const NodeDescription& description,
                           std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_native(description.reads, description.writes,
                                                      std::get<WorkerWork>(description.work), {},
                                                      registration);
                bake_width(node, description.width);
                return node;
            }

            /** @brief Adds a State rule, ordered by the fields the State has at this build. */
            Node& add_state(TaskGraph& backing_graph, const NodeDescription& description,
                            std::optional<std::uint32_t> registration)
            {
                std::vector<BufferRegion> reads;
                std::vector<BufferRegion> writes;
                description.state_access(reads, writes);
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    reads, writes, {}, registration);
                bake_width(node, description.width);
                attach_live_fields(node, description.live);
                return node;
            }

            /** @brief Adds a command group that carries per-step controls. */
            Node& add_dynamic(TaskGraph& backing_graph, const NodeDescription& description,
                              std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    description.reads, description.writes,
                                                    engine_controls(description.bindings), {},
                                                    registration);
                bake_width(node, description.width);
                return node;
            }

            /** @brief Adds a State rule that carries per-step controls. */
            Node& add_dynamic_state(TaskGraph& backing_graph, const NodeDescription& description,
                                    std::optional<std::uint32_t> registration)
            {
                std::vector<BufferRegion> reads;
                std::vector<BufferRegion> writes;
                description.state_access(reads, writes);
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    reads, writes,
                                                    engine_controls(description.bindings), {},
                                                    registration);
                bake_width(node, description.width);
                attach_live_fields(node, description.live);
                return node;
            }

            /** @brief One function per NodeKind, in the order of the enumerators. */
            constexpr Emitter K_EMITTERS[] = {
                add_kernel, add_untracked, add_task, add_host,
                add_state,  add_dynamic,   add_dynamic_state,
            };

            static_assert(std::size(K_EMITTERS) ==
                              static_cast<std::size_t>(NodeKind::DynamicState) + 1,
                          "Every NodeKind has one function in K_EMITTERS.");
        } // namespace

        Internal::Graph::Node& add_described_node(Internal::Graph::TaskGraph& backing_graph,
                                                  const NodeDescription& description,
                                                  std::optional<std::uint32_t> registration)
        {
            const Emitter emit = K_EMITTERS[static_cast<std::size_t>(description.kind)];
            return emit(backing_graph, description, registration);
        }

        void apply_stamp(const NodeStamp& stamp, Internal::Graph::Node& node) noexcept
        {
            if (stamp.name != nullptr)
                node.metadata.name = stamp.name;
            if (stamp.operation.is_named())
                node.metadata.op_id = stamp.operation;
            for (std::size_t i = 0; i < stamp.parameter_count; ++i)
                node.metadata.set_param<std::uint64_t>(static_cast<int>(i), stamp.parameters[i]);
            if (stamp.width != ::SushiRuntime::K_WIDTH_UNKNOWN)
                node.baked_width = stamp.width;
        }
    } // namespace Detail
} // namespace SushiRuntime
