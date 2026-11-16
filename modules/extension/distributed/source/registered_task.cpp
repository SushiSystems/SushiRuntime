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
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/api/graph/access.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Returns the whole-buffer region of each buffer, in order: its handle, or the
             *        Borrowed handle the recorder keeps for its view's range when it has none.
             * @pre Every view without a handle is a range the recorder can borrow; a refusal stops the process.
             */
            std::vector<BufferRegion> regions_of(Recorder& recorder,
                                                 SushiRuntime::span<const TaskBuffer> buffers)
            {
                std::vector<BufferRegion> regions;
                regions.reserve(buffers.size());
                for (const TaskBuffer& buffer : buffers)
                {
                    if (!buffer.handle.is_null())
                    {
                        regions.emplace_back(buffer.handle);
                        continue;
                    }
                    const BufferView& view = buffer.view;
                    Result<BufferRegion> region = recorder.borrowed_region(view.data, view.size_bytes);
                    SR_VERIFY(static_cast<bool>(region),
                              ("record_registered_task: a buffer view cannot be named as a region: " +
                               (region ? std::string() : region.error().message) +
                               "; pass views of whole, non-overlapping buffers.")
                                  .c_str());
                    regions.push_back(*region);
                }
                return regions;
            }

            /** @brief Returns the views of buffers, in order. */
            std::vector<BufferView> views_of(SushiRuntime::span<const TaskBuffer> buffers)
            {
                std::vector<BufferView> views;
                views.reserve(buffers.size());
                for (const TaskBuffer& buffer : buffers)
                    views.push_back(buffer.view);
                return views;
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
                                           SushiRuntime::span<const TaskBuffer> inputs,
                                           SushiRuntime::span<const TaskBuffer> outputs)
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
            call.inputs = views_of(inputs);
            call.outputs = views_of(outputs);

            return recorder.task(Access(regions_of(recorder, inputs), regions_of(recorder, outputs)), std::move(call),
                                 options);
        }
    } // namespace Distributed
} // namespace SushiRuntime
