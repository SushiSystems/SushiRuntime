/**************************************************************************/
/* dispatch_job.cpp                                                       */
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
 * @file dispatch_job.cpp
 * @brief Implements DispatchEntry: the run of one kernel and the copy of its outputs.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#include <SushiRuntime/dispatch_executor/dispatch_job.hpp>

#include <exception>
#include <utility>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/span.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Returns the options a kernel is handed for a dispatch.
             * @return A value that borrows the name and the parameters of @p dispatch, so it
             *         is used before the dispatch is destroyed.
             */
            NodeOptions options_of(const Wire::TaskDispatchMessage& dispatch) noexcept
            {
                NodeOptions options;
                options.name = dispatch.name;
                options.operation = dispatch.operation;
                options.parameters = SushiRuntime::span<const std::uint64_t>(
                    dispatch.parameters.data(), dispatch.parameter_count);
                return options;
            }

            /** @brief Copies each output of an entry so the stream sends what the kernel left. */
            void copy_outputs(DispatchEntry& entry)
            {
                entry.copies.resize(entry.outputs.size());
                for (std::size_t index = 0; index < entry.outputs.size(); ++index)
                {
                    const std::byte* first =
                        static_cast<const std::byte*>(entry.outputs[index].data);
                    entry.copies[index].assign(first, first + entry.outputs[index].size_bytes);
                }
            }
        } // namespace

        DispatchEntry::DispatchEntry(DispatchJob queued, const KernelRegistry::Kernel& registered,
                                     std::vector<BufferView> input_views,
                                     std::vector<BufferView> output_views) noexcept
            : job(std::move(queued)), kernel(&registered), inputs(std::move(input_views)),
              outputs(std::move(output_views))
        {

        }

        void DispatchEntry::execute(sycl::queue& queue,
                                    const std::vector<sycl::event>& dependencies) noexcept
        {
            try
            {
                // The kernel takes no events, so the tasks it follows must end first.
                for (sycl::event dependency : dependencies)
                    dependency.wait();

                sycl::event event = (*kernel)(
                    queue, SushiRuntime::span<const BufferView>(inputs.data(), inputs.size()),
                    SushiRuntime::span<const BufferView>(outputs.data(), outputs.size()),
                    options_of(job.dispatch));
                event.wait_and_throw();

                copy_outputs(*this);
                ran = true;
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }
            catch (...)
            {
                failure = "unknown exception";
            }
        }

        void DispatchEntry::execute_host() noexcept
        {
            try
            {
                (*host_kernel)(SushiRuntime::span<const BufferView>(inputs.data(), inputs.size()),
                               SushiRuntime::span<const BufferView>(outputs.data(), outputs.size()),
                               options_of(job.dispatch));

                copy_outputs(*this);
                ran = true;
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }
            catch (...)
            {
                failure = "unknown exception";
            }
        }


    } // namespace Distributed
} // namespace SushiRuntime
