/**************************************************************************/
/* dispatch_plan.cpp                                                      */
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
 * @file dispatch_plan.cpp
 * @brief Implements the pure decisions of the executor: admission, job verdict and batch order.
 * @author Mustafa Garip
 * @see modules/extension/dispatch_executor/README.md
 */

#include <SushiRuntime/dispatch_executor/dispatch_plan.hpp>

#include <algorithm>

#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the chunk size used when a dispatch arrives with none. */
            constexpr std::uint32_t K_DEFAULT_CHUNK_BYTES = 256u * 1024u;

            /** @brief Holds the most data bytes one BUFFER_CHUNK frame can carry. */
            constexpr std::uint32_t K_MAX_CHUNK_BYTES =
                Wire::K_MAX_FRAME_PAYLOAD -
                static_cast<std::uint32_t>(Wire::BufferChunkMessage::K_PREFIX_BYTES);
        } // namespace

        EnqueueStatus admit_dispatch(std::size_t queued, std::size_t max_jobs,
                                     bool stopping) noexcept
        {
            if (stopping)
                return EnqueueStatus::Stopping;
            return queued >= max_jobs ? EnqueueStatus::QueueFull : EnqueueStatus::Queued;
        }

        JobVerdict judge_job(bool kernel_known, std::uint32_t device_index,
                             std::size_t device_count, bool shares_cache_context) noexcept
        {
            if (!kernel_known)
                return JobVerdict::UnknownOperation;
            if (device_index >= device_count)
                return JobVerdict::DeviceMissing;
            return shares_cache_context ? JobVerdict::Run : JobVerdict::DeviceOutsideCacheContext;
        }

        bool reads_a_written_buffer(SushiRuntime::span<const BufferHandle> inputs,
                                    SushiRuntime::span<const GlobalBufferID> written) noexcept
        {
            for (const BufferHandle& input : inputs)
                for (const GlobalBufferID id : written)
                    if (input.id == id)
                        return true;
            return false;
        }

        std::uint32_t usable_chunk_bytes(std::uint32_t chunk_bytes) noexcept
        {
            if (chunk_bytes == 0)
                return K_DEFAULT_CHUNK_BYTES;
            return std::min(chunk_bytes, K_MAX_CHUNK_BYTES);
        }

    } // namespace Distributed
} // namespace SushiRuntime
