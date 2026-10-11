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
 * @brief Implements record_registered_task(), a task whose local work is a registered kernel.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include <SushiRuntime/distributed/registered_task.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns the whole-buffer region of each view, in the views' order. */
            std::vector<Internal::Memory::ResourceRegion> regions_of(
                SushiRuntime::span<const BufferView> views)
            {
                std::vector<Internal::Memory::ResourceRegion> regions;
                regions.reserve(views.size());
                for (const BufferView& view : views)
                    regions.emplace_back(view.data);
                return regions;
            }

            /**
             * @brief Keeps what a registered kernel is handed, with nothing borrowed from the
             *        caller of record_registered_task().
             */
            struct RegisteredCall
            {
                KernelRegistry::Kernel kernel;         /**< The kernel, copied from the registry. */
                std::string name;                      /**< The node's label. */
                OperationID operation;                 /**< The operation the kernel serves. */
                std::size_t width = 0;                 /**< The width the caller stated. */
                std::vector<std::uint64_t> parameters; /**< The scalars the caller gave. */
                std::vector<BufferView> inputs;        /**< The buffers read, in order. */
                std::vector<BufferView> outputs;       /**< The buffers written, in order. */

                /** @brief Runs the kernel on the queue the scheduler hands the node. */
                sycl::event operator()(sycl::queue& queue, const std::vector<sycl::event>&) const
                {
                    NodeOptions described;
                    described.width = width;
                    described.name = name;
                    described.operation = operation;
                    described.parameters = SushiRuntime::span<const std::uint64_t>(
                        parameters.data(), parameters.size());
                    return kernel(
                        queue,
                        SushiRuntime::span<const BufferView>(inputs.data(), inputs.size()),
                        SushiRuntime::span<const BufferView>(outputs.data(), outputs.size()),
                        described);
                }
            };
        } // namespace

        std::size_t record_registered_task(Recorder& recorder, const KernelRegistry& kernels,
                                           const NodeOptions& options,
                                           SushiRuntime::span<const BufferView> inputs,
                                           SushiRuntime::span<const BufferView> outputs)
        {
            const KernelRegistry::Kernel* kernel = kernels.find(options.operation);
            SR_VERIFY(kernel != nullptr,
                      ("record_registered_task: no kernel is registered for operation " +
                       std::to_string(options.operation.value) + " (task '" +
                       std::string(options.name) +
                       "'); call KernelRegistry::register_kernel() for it first.")
                          .c_str());

            RegisteredCall call;
            call.kernel = *kernel;
            call.name = std::string(options.name);
            call.operation = options.operation;
            call.width = options.width;
            call.parameters.assign(options.parameters.begin(), options.parameters.end());
            call.inputs.assign(inputs.begin(), inputs.end());
            call.outputs.assign(outputs.begin(), outputs.end());

            return recorder.task(Access(regions_of(inputs), regions_of(outputs)), std::move(call),
                                 options);
        }

    } // namespace Distributed
} // namespace SushiRuntime
