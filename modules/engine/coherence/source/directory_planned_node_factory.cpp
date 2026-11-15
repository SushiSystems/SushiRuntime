/**************************************************************************/
/* directory_planned_node_factory.cpp                                     */
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
 * @file directory_planned_node_factory.cpp
 * @brief Implements DirectoryPlannedNodeFactory.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/directory_planned_node_factory.hpp>

#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the address of handle's instance on space, or null when it has none. */
            void* instance_on(const Memory::ResidencyView& directory, GlobalHandle handle,
                              MemorySpaceID space) noexcept
            {
                const ResolveOutcome outcome = directory.resolve(handle, space);
                return outcome.status == HandleStatus::Ok ? outcome.address : nullptr;
            }

            /** @brief Returns the device a step's node runs on: the device of its space, else 0. */
            std::size_t device_of_step(const PlannedStep& step) noexcept
            {
                const std::optional<std::size_t> device = Graph::device_of_space(step.space);
                if (device)
                    return *device;
                const std::optional<std::size_t> source = Graph::device_of_space(step.source);
                return step.kind == PlannedStepKind::Copy && source ? *source : 0;
            }

            /** @brief Returns the label a planned node of kind carries. */
            const char* label_of(PlannedStepKind kind) noexcept
            {
                switch (kind)
                {
                    case PlannedStepKind::Allocate: return "coherence.allocate";
                    case PlannedStepKind::Copy:     return "coherence.copy";
                    case PlannedStepKind::Free:     return "coherence.free";
                }
                return "coherence.step";
            }
        } // namespace

        DirectoryPlannedNodeFactory::DirectoryPlannedNodeFactory(Memory::ResidencyView& directory,
                                                                 std::shared_ptr<InstanceAllocator> allocator,
                                                                 std::vector<TransferPath*> paths)
            : directory_(&directory), allocator_(std::move(allocator)), paths_(std::move(paths))
        {

        }

        Result<sushi_ptr<Graph::Node>> DirectoryPlannedNodeFactory::make(const PlannedStep& step)
        {
            if (step.kind == PlannedStepKind::Copy && step.path >= paths_.size())
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "a planned copy names a transfer path the factory does not hold."});

            sushi_ptr<Graph::Node> node = make_sushi<Graph::Node>();
            node->metadata.name = label_of(step.kind);
            node->mode = Graph::Node::ExecutionMode::Host;
            const BufferRegion range(step.handle, step.begin, step.end - step.begin);
            node->io_writes_.push_back(range);
            if (step.kind == PlannedStepKind::Copy)
                node->io_reads_.push_back(range);

            // The step names its device; the dispatch does not place it again.
            node->cached_queue_index_ = static_cast<int>(device_of_step(step));
            node->locality_checked_ = true;

            Memory::ResidencyView* const directory = directory_;
            const std::shared_ptr<InstanceAllocator> allocator = allocator_;
            switch (step.kind)
            {
                case PlannedStepKind::Allocate:
                    node->host_work = [directory, allocator, step](sycl::queue&,
                                                                   const std::vector<sycl::event>& dependencies)
                    {
                        sycl::event::wait(dependencies);
                        if (instance_on(*directory, step.handle, step.space) != nullptr)
                            return sycl::event{};
                        void* const address = allocator->allocate(step.space, step.end, step.alignment);
                        if (address == nullptr)
                            throw std::bad_alloc();
                        if (directory->set_instance(step.handle, step.space, address,
                                                    allocator->kind_of(step.space)) != HandleStatus::Ok)
                        {
                            allocator->release(step.space, address);
                            throw std::runtime_error("coherence.allocate: the residency directory "
                                                     "refused the new instance.");
                        }
                        return sycl::event{};
                    };
                    break;

                case PlannedStepKind::Copy:
                {
                    TransferPath* const path = paths_[step.path];
                    node->host_work = [directory, path, step](sycl::queue&,
                                                              const std::vector<sycl::event>& dependencies)
                    {
                        const auto* const from =
                            static_cast<const unsigned char*>(instance_on(*directory, step.handle, step.source));
                        auto* const to = static_cast<unsigned char*>(instance_on(*directory, step.handle, step.space));
                        if (from == nullptr || to == nullptr)
                            throw std::runtime_error("coherence.copy: an instance the plan copies "
                                                     "between is missing.");
                        const TransferRequest request{step.source, step.space, from + step.begin,
                                                      to + step.begin, step.end - step.begin};
                        return path->copy(request, dependencies);
                    };
                    break;
                }

                case PlannedStepKind::Free:
                    node->host_work = [directory, allocator, step](sycl::queue&,
                                                                   const std::vector<sycl::event>& dependencies)
                    {
                        sycl::event::wait(dependencies);
                        void* const address = instance_on(*directory, step.handle, step.space);
                        if (address == nullptr)
                            return sycl::event{};
                        static_cast<void>(directory->drop_instance(step.handle, step.space));
                        allocator->release(step.space, address);
                        return sycl::event{};
                    };
                    break;
            }
            return node;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
