/**************************************************************************/
/* kernel_registry.hpp                                                    */
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
 * @file kernel_registry.hpp
 * @brief Declares KernelRegistry, which maps an operation id to the kernel that runs it.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Maps an operation id to a kernel compiled into this binary.
         *
         * Master and worker each build their own registry, so only the id crosses the wire.
         * Register every kernel before the registry is given to a WorkerAgent, run_worker()
         * or a Cluster; from then on it is only read, by any thread.
         */
        class SUSHIRUNTIME_API KernelRegistry : public Core::RefCounted
        {
            public:
                /**
                 * @brief Names a kernel: it submits work on @p queue and returns its event.
                 *
                 * The views name the task's buffers in the order the task lists them and are
                 * valid until the returned event completes. A kernel may throw; the worker
                 * then reports the task as failed.
                 */
                using Kernel = std::function<sycl::event(
                    sycl::queue& queue, SushiRuntime::span<const BufferView> inputs,
                    SushiRuntime::span<const BufferView> outputs,
                    const Graph::TaskMetadata& metadata)>;

                /** @brief Sets the kernel of an operation, replacing the one set before. */
                void register_kernel(Graph::OpID id, Kernel kernel);

                /** @brief Returns whether an operation has a kernel. */
                [[nodiscard]] bool has(Graph::OpID id) const;

                /**
                 * @brief Returns the kernel of an operation.
                 * @return Null when the operation has none; valid while the registry lives.
                 */
                [[nodiscard]] const Kernel* find(Graph::OpID id) const;

                /** @brief Returns the id of every operation that has a kernel, in no set order. */
                [[nodiscard]] std::vector<Graph::OpID> known_ops() const;

            private:
                /** @brief Holds the kernels by the value of their operation id. */
                std::unordered_map<std::uint32_t, Kernel> kernels_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
