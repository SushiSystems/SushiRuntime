/**************************************************************************/
/* registered_task.hpp                                                    */
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
 * @file registered_task.hpp
 * @brief Declares add_registered_task(), which adds a task that runs a registered kernel.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;
        class TaskGraph;
    } // namespace Graph

    namespace Distributed
    {
        /**
         * @brief Adds a task that runs the kernel registered for metadata.op_id on this machine.
         *
         * A worker with the same operation may run it instead once a Cluster is attached and
         * every buffer is shared. The kernel is copied into the node, which the graph owns.
         * @param inputs The buffers read, in the kernel's order; the memory outlives the run.
         * @throws Core::SushiException When @p kernels has no kernel for the operation.
         */
        SUSHIRUNTIME_API Graph::Node& add_registered_task(
            Graph::TaskGraph& graph, const KernelRegistry& kernels,
            const Graph::TaskMetadata& metadata, SushiRuntime::span<const BufferView> inputs,
            SushiRuntime::span<const BufferView> outputs);

    } // namespace Distributed
} // namespace SushiRuntime
