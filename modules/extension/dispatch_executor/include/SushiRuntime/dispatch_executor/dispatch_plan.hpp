/**************************************************************************/
/* dispatch_plan.hpp                                                      */
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
 * @file dispatch_plan.hpp
 * @brief Declares the pure decisions of the executor: admission, job verdict and batch order.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names what enqueue() did with a dispatch.
         */
        enum class EnqueueStatus : std::uint8_t
        {
            Queued,    /**< The job waits for the exec thread; send TASK_ACCEPTED. */
            QueueFull, /**< The queue is at max_jobs; send nothing, the master takes it back. */
            Stopping,  /**< stop() has begun; the executor queued a ShuttingDown result itself. */
        };

        /**
         * @brief Names whether a job can run on this worker, and if not, why.
         */
        enum class JobVerdict : std::uint8_t
        {
            Run,                      /**< The kernel and the device are usable. */
            UnknownOperation,         /**< The worker has no kernel for the operation. */
            DeviceMissing,            /**< The dispatch names a device the worker does not have. */
            DeviceOutsideCacheContext /**< The device does not share the cache's context. */
        };

        /**
         * @brief Decides what enqueue() does with a dispatch.
         * @param queued The jobs waiting now.
         * @param max_jobs The bound on the queue.
         * @param stopping Whether stop() has begun.
         */
        [[nodiscard]] SUSHIRUNTIME_API EnqueueStatus admit_dispatch(
            std::size_t queued, std::size_t max_jobs, bool stopping) noexcept;

        /**
         * @brief Decides whether a job can run: the kernel is checked first, then the device.
         * @param kernel_known Whether the registry has the job's operation.
         * @param device_index The device the dispatch names.
         * @param device_count The devices the worker has.
         * @param shares_cache_context Whether that device shares the context of the cache.
         */
        [[nodiscard]] SUSHIRUNTIME_API JobVerdict judge_job(
            bool kernel_known, std::uint32_t device_index, std::size_t device_count,
            bool shares_cache_context) noexcept;

        /**
         * @brief Tells whether a job reads a buffer that a job of the running batch writes.
         *
         * Such a buffer is in no cache yet, so the job opens the next batch.
         */
        [[nodiscard]] SUSHIRUNTIME_API bool reads_a_written_buffer(
            SushiRuntime::span<const BufferHandle> inputs,
            SushiRuntime::span<const GlobalBufferID> written) noexcept;

        /** @brief Returns a chunk size one frame can carry; 0 takes the default of 256 KiB. */
        [[nodiscard]] SUSHIRUNTIME_API std::uint32_t usable_chunk_bytes(
            std::uint32_t chunk_bytes) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
