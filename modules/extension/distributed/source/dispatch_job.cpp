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
 * @file dispatch_job.cpp
 * @brief Implements DispatchEntry: the run of one kernel and the copy of its outputs.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "dispatch_job.hpp"

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

                copies.resize(outputs.size());
                for (std::size_t index = 0; index < outputs.size(); ++index)
                {
                    const std::byte* first = static_cast<const std::byte*>(outputs[index].data);
                    copies[index].assign(first, first + outputs[index].size_bytes);
                }
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
