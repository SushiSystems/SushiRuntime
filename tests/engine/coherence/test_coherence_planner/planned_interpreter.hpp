/**************************************************************************/
/* planned_interpreter.hpp                                                */
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
 * @file planned_interpreter.hpp
 * @brief Declares run_planned, which runs a graph through CoherencePlanner and its planned nodes.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

#include "support/fake_transfer_path.hpp"
#include "graph_case.hpp"
#include "node_operation.hpp"

namespace SushiRuntime
{
    namespace Internal::Coherence::Equivalence
    {
        /** @brief Holds what one planned run leaves: the outcome, the plan and what the plan left behind. */
        struct PlannedRun
        {
            Outcome outcome;                /**< Bytes at the end and what each node read. */
            std::vector<PlannedStep> steps; /**< The plan that was run. */
            std::size_t leftover = 0;       /**< Instances still held outside the home space. */
        };

        /** @brief Hands out host memory for the instances of the spaces and counts what is still held. */
        class HeapInstanceAllocator final : public InstanceAllocator
        {
            public:
                [[nodiscard]] void* allocate(MemorySpaceID, std::size_t bytes, std::size_t) override
                {
                    ++held_;
                    return std::malloc(bytes);
                }

                void release(MemorySpaceID, void* address) noexcept override
                {
                    --held_;
                    std::free(address);
                }

                /** @brief Names every instance device memory, so the planner frees what it allocated. */
                [[nodiscard]] std::optional<Memory::USMKind> kind_of(MemorySpaceID) const noexcept override
                {
                    return Memory::USMKind::Device;
                }

                /** @brief Returns the allocations not yet released. */
                [[nodiscard]] int held() const noexcept
                {
                    return held_;
                }

            private:
                /// @brief Allocations not yet released.
                int held_ = 0;
        };

        namespace Detail
        {
            /** @brief Returns the space of device; device 0 is the home space. */
            inline MemorySpaceID space_of_device(std::size_t device) noexcept
            {
                return MemorySpaceID(static_cast<std::uint32_t>(device + 1));
            }

            /** @brief Returns the planner's view of node: the spaces and ranges it touches. */
            inline NodeAccess access_of(const GraphNode& node, const std::vector<GlobalHandle>& handles)
            {
                NodeAccess access;
                access.planned = true;
                access.space = space_of_device(node.device);
                for (const ByteAccess& read : node.reads)
                    access.reads.emplace_back(handles[read.buffer], read.offset, read.length);
                for (const ByteAccess& write : node.writes)
                    access.writes.emplace_back(handles[write.buffer], write.offset, write.length);
                return access;
            }

            /** @brief Runs the node a factory builds for step and waits for its event. */
            inline void run_step(PlannedNodeFactory& factory, const PlannedStep& step, sycl::queue& queue)
            {
                const Result<sushi_ptr<Graph::Node>> node = factory.make(step);
                if (!node)
                    throw std::runtime_error("the factory refused a planned step: " + node.error().message);
                node.value()->host_work(queue, {}).wait();
            }
        } // namespace Detail

        /**
         * @brief Runs graph on its devices through the planner and the planned nodes.
         *
         * The buffers start on device 0, in memory the run owns. Each device is a memory space of
         * host memory, so the run needs no accelerator. A plan step runs before the node it names,
         * the steps at the graph end run last, and the bytes of device 0 are the result.
         * @throws std::runtime_error when the plan or a step is refused or an instance is missing.
         */
        [[nodiscard]] inline PlannedRun run_planned(const GraphCase& graph, sycl::queue& queue)
        {
            using Memory::DeviceMemorySpace;
            using Memory::DeviceMemorySpaceDescription;
            using Memory::ElementLayout;
            using Memory::MemorySpace;

            FixedMemoryProbe probe;
            std::vector<std::unique_ptr<MemorySpace>> owned;
            std::vector<const MemorySpace*> spaces;
            for (std::size_t device = 0; device < graph.device_count; ++device)
            {
                const MemorySpaceID id = Detail::space_of_device(device);
                owned.push_back(std::make_unique<DeviceMemorySpace>(
                    DeviceMemorySpaceDescription{id, id.value(), {}}, probe));
                spaces.push_back(owned.back().get());
            }

            Memory::ResidencyDirectory directory{0};
            PlannedRun run;
            run.outcome.buffers = graph.initial;
            std::vector<GlobalHandle> handles;
            for (std::vector<unsigned char>& bytes : run.outcome.buffers)
            {
                const GlobalHandle handle = directory.mint(HandleKind::Buffer, HandleFlags::None).handle;
                const MemorySpaceID home = Detail::space_of_device(0);
                if (directory.describe(handle, bytes.size(), ElementLayout{}, home) != HandleStatus::Ok ||
                    directory.set_instance(handle, home, bytes.data()) != HandleStatus::Ok)
                    throw std::runtime_error("the directory refused a buffer.");
                handles.push_back(handle);
            }

            FakeTransferPath path;
            CoherencePlanner planner(directory, spaces, {&path}, uniform_transfer_cost());
            std::vector<NodeAccess> accesses;
            for (const GraphNode& node : graph.nodes)
                accesses.push_back(Detail::access_of(node, handles));
            const Result<CopyPlan> plan = planner.plan(accesses);
            if (!plan)
                throw std::runtime_error("the planner refused the graph: " + plan.error().message);
            run.steps = plan.value().steps;

            const auto allocator = std::make_shared<HeapInstanceAllocator>();
            DirectoryPlannedNodeFactory factory(directory, allocator, {&path});
            std::size_t next = 0;
            for (std::size_t index = 0; index < graph.nodes.size(); ++index)
            {
                for (; next < run.steps.size() && run.steps[next].node == index; ++next)
                    Detail::run_step(factory, run.steps[next], queue);
                const GraphNode& node = graph.nodes[index];
                const MemorySpaceID space = Detail::space_of_device(node.device);
                run.outcome.digests.push_back(run_node(node, [&](std::size_t buffer)
                {
                    const ResolveOutcome found = directory.resolve(handles[buffer], space);
                    if (found.status != HandleStatus::Ok)
                        throw std::runtime_error("a node found no instance on its device.");
                    return static_cast<unsigned char*>(found.address);
                }));
            }
            for (; next < run.steps.size(); ++next)
                Detail::run_step(factory, run.steps[next], queue);

            run.leftover = static_cast<std::size_t>(allocator->held());
            for (const GlobalHandle handle : handles)
                for (std::size_t device = 1; device < graph.device_count; ++device)
                    if (directory.resolve(handle, Detail::space_of_device(device)).status == HandleStatus::Ok)
                        ++run.leftover;
            return run;
        }
    } // namespace Internal::Coherence::Equivalence
} // namespace SushiRuntime
