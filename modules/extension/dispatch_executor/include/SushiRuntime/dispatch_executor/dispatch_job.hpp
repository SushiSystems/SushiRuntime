/**************************************************************************/
/* dispatch_job.hpp                                                       */
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
 * @file dispatch_job.hpp
 * @brief Declares DispatchJob, a queued dispatch, and DispatchEntry, a job of a batch.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Holds one dispatch that waits for the exec thread. */
        struct DispatchJob
        {
            /** @brief The connection every reply goes to. */
            PeerID from;

            /** @brief The task. */
            Wire::TaskDispatchMessage dispatch;

            /** @brief Data bytes per output chunk. */
            std::uint32_t chunk_bytes = 0;

            /** @brief What HeldTasks::hold() returned for the task. */
            std::uint64_t generation = 0;
        };

        /**
         * @brief Holds one job of a batch from its pins to its outcome.
         *
         * Its task writes ran, failure and copies on a pool thread while the batch runs; the
         * exec thread reads them only after the run returned.
         */
        struct DispatchEntry
        {
            /** @brief Builds the entry of a job whose buffers are pinned. */
            DispatchEntry(DispatchJob queued, const KernelRegistry::Kernel& registered,
                          std::vector<BufferView> input_views,
                          std::vector<BufferView> output_views) noexcept;

            /**
             * @brief Waits for the tasks before it, runs the kernel, waits for it and copies
             *        each output; never throws.
             */
            void execute(sycl::queue& queue, const std::vector<sycl::event>& dependencies) noexcept;

            /// @brief The dispatch and where its replies go.
            DispatchJob job;

            /// @brief The kernel of the dispatch's operation.
            const KernelRegistry::Kernel* kernel;

            /// @brief One cache pin per input and output; the batch drops them.
            std::vector<GlobalBufferID> pins;

            /// @brief The memory of each input.
            std::vector<BufferView> inputs;

            /// @brief The memory of each output.
            std::vector<BufferView> outputs;

            /// @brief The bytes of each output once the kernel ended; the stream sends them.
            std::vector<std::vector<std::byte>> copies;

            /// @brief Why the task did not run to its end; empty when it did or never started.
            std::string failure;

            /// @brief Whether the kernel ran to its end and the outputs were copied.
            bool ran = false;
        };

    } // namespace Distributed
} // namespace SushiRuntime
