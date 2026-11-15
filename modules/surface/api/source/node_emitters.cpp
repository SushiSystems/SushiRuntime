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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/submit_form.hpp>
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

            /** @brief Returns the engine's form of a described node's submission. */
            Internal::Graph::SubmitForm engine_form(SubmitForm form) noexcept
            {
                switch (form)
                {
                    case SubmitForm::Fixed:
                        return Internal::Graph::SubmitForm::Fixed;
                    case SubmitForm::LateWindow:
                        return Internal::Graph::SubmitForm::LateWindow;
                    case SubmitForm::DeviceSized:
                        return Internal::Graph::SubmitForm::DeviceSized;
                    case SubmitForm::StepBound:
                        break;
                }
                return Internal::Graph::SubmitForm::StepBound;
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

            /** @brief Adds a command group that names no region, on the device it states. */
            Node& add_untracked(TaskGraph& backing_graph, const NodeDescription& description,
                                std::optional<std::uint32_t> registration)
            {
                Node& node = backing_graph.add_node(std::get<CommandGroupWork>(description.work),
                                                    std::vector<sycl::event>{}, registration);
                // A command group that states no device is placed by the compile's rule, else 0.
                if (description.device)
                    node.place_on(*description.device);
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
                // A task that states no device is placed by the owner of its memory.
                if (description.device)
                    node.place_on(*description.device);
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
            Internal::Graph::Node& node = emit(backing_graph, description, registration);
            node.host_form = description.host_form;
            node.submit_form = engine_form(description.submit_form);
            node.kernel_addressing = description.rebindable
                ? Internal::Graph::KernelAddressing::Rebindable
                : Internal::Graph::KernelAddressing::FixedToHome;
            return node;
        }

        void attach_kernel_pointers(Internal::Graph::Node& node, const NodeDescription& description,
                                    const Internal::Memory::ResidencyView& handles,
                                    std::size_t device_count)
        {
            if (description.pointer_handles.empty())
                return;
            std::vector<Internal::Graph::KernelPointerSlot> slots;
            slots.reserve(description.pointer_handles.size());
            for (std::size_t index = 0; index < description.pointer_handles.size(); ++index)
            {
                const GlobalHandle handle = description.pointer_handles[index];
                // A task bound by handle records no home address; the directory gives it.
                if (index < description.pointer_homes.size())
                    slots.push_back(Internal::Graph::make_kernel_pointer_slot(
                        handles, handle, description.pointer_homes[index], device_count));
                else
                    slots.push_back(
                        Internal::Graph::make_kernel_pointer_slot(handles, handle, device_count));
            }
            node.kernel_pointers_ =
                std::make_unique<Internal::Graph::KernelPointers>(std::move(slots), handles);
        }

        void apply_stamp(const NodeStamp& stamp, Internal::Graph::Node& node) noexcept
        {
            if (stamp.name != nullptr)
                node.metadata.name = stamp.name;
            node.metadata.origin = stamp.origin;
            if (stamp.operation.is_named())
                node.metadata.op_id = stamp.operation;
            if (stamp.placement_class.is_named())
                node.metadata.placement_class = stamp.placement_class;
            if (stamp.placement_key.has_value())
                node.metadata.placement_key = stamp.placement_key;
            if (stamp.determinism != Determinism::Unstated)
                node.metadata.determinism = stamp.determinism;
            node.metadata.cost = stamp.cost;
            node.declared_split = stamp.split;
            for (std::size_t i = 0; i < stamp.parameter_count; ++i)
                node.metadata.set_param<std::uint64_t>(static_cast<int>(i), stamp.parameters[i]);
            if (stamp.width != ::SushiRuntime::K_WIDTH_UNKNOWN)
                node.baked_width = stamp.width;
        }
    } // namespace Detail
} // namespace SushiRuntime
