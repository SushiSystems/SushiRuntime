/**************************************************************************/
/* registered_task.cpp                                                    */
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
 * @file registered_task.cpp
 * @brief Implements add_registered_task(), a graph task whose local work is a registered kernel.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include <SushiRuntime/distributed/registered_task.hpp>

#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task_work.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns the whole-buffer region of each view, in the views' order. */
            std::vector<Memory::ResourceRegion> regions_of(
                SushiRuntime::span<const BufferView> views)
            {
                std::vector<Memory::ResourceRegion> regions;
                regions.reserve(views.size());
                for (const BufferView& view : views)
                    regions.emplace_back(view.data);
                return regions;
            }
        } // namespace

        Graph::Node& add_registered_task(Graph::TaskGraph& graph, const KernelRegistry& kernels,
                                         const Graph::TaskMetadata& metadata,
                                         SushiRuntime::span<const BufferView> inputs,
                                         SushiRuntime::span<const BufferView> outputs)
        {
            const KernelRegistry::Kernel* kernel = kernels.find(metadata.op_id);
            if (kernel == nullptr)
                throw Core::SushiException(Core::Error{
                    Core::Errc::invalid_graph,
                    "add_registered_task: no kernel is registered for operation " +
                        std::to_string(metadata.op_id.value) + " (task '" +
                        (metadata.name != nullptr ? metadata.name : "unnamed_task") +
                        "'); call KernelRegistry::register_kernel() for it first."});

            Graph::HostWork local_work =
                [kernel = *kernel, metadata,
                 inputs = std::vector<BufferView>(inputs.begin(), inputs.end()),
                 outputs = std::vector<BufferView>(outputs.begin(), outputs.end())](
                    sycl::queue& queue, const std::vector<sycl::event>&) -> sycl::event
                {
                    return kernel(
                        queue,
                        SushiRuntime::span<const BufferView>(inputs.data(), inputs.size()),
                        SushiRuntime::span<const BufferView>(outputs.data(), outputs.size()),
                        metadata);
                };

            return graph.add_task(metadata, regions_of(inputs), regions_of(outputs),
                                  std::move(local_work));
        }

    } // namespace Distributed
} // namespace SushiRuntime
