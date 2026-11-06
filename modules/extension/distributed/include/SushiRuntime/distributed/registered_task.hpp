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
 * @file registered_task.hpp
 * @brief Declares record_registered_task(), which records a task that runs a registered kernel.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

namespace SushiRuntime
{
    class Recorder;
    struct NodeOptions;

    namespace Distributed
    {
        /**
         * @brief Records a task that runs the kernel registered for options.operation here, or
         *        on a worker with that operation once a Cluster is attached.
         * @param inputs The buffers read, in the kernel's order; the memory outlives the run.
         * @return The position of the rule recorded, or rule_count() when options are refused.
         * @pre kernels holds a kernel for options.operation.
         * @see modules/extension/distributed/README.md
         */
        SUSHIRUNTIME_API std::size_t record_registered_task(
            Recorder& recorder, const KernelRegistry& kernels, const NodeOptions& options,
            SushiRuntime::span<const BufferView> inputs,
            SushiRuntime::span<const BufferView> outputs);

    } // namespace Distributed
} // namespace SushiRuntime
