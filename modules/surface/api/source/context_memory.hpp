/**************************************************************************/
/* context_memory.hpp                                                     */
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
 * @file context_memory.hpp
 * @brief Defines the allocation of USM by residency and the lookup of an owning queue.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#pragma once

#include <cstddef>
#include <optional>

#include <sycl/sycl.hpp>

#include <SushiRuntime/api/api_log_category.hpp>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns the kind of USM that holds memory of a residency.
         * @pre residency is an enumerator of Residency.
         */
        [[nodiscard]] inline Internal::Memory::USMKind usm_kind_for(Residency residency) noexcept
        {
            switch (residency)
            {
                case Residency::Device: return Internal::Memory::USMKind::Device;
                case Residency::Host:   return Internal::Memory::USMKind::Host;
                case Residency::Shared: return Internal::Memory::USMKind::Shared;
                case Residency::Planned:
                    SR_FATAL("usm_kind_for(): a Planned buffer is never allocated as one USM kind");
            }
            SR_FATAL("usm_kind_for(): the value is no enumerator of Residency");
        }

        /**
         * @brief Allocates USM of the given residency on one device queue.
         * @param context   Performs and records the allocation.
         * @param alignment Byte alignment of the address; 0 asks for the device default.
         * @return The USM pointer, or nullptr on failure.
         */
        inline void* malloc_residency_on(Internal::Execution::RuntimeContext& context,
                                         std::size_t bytes,
                                         DeviceIndex device_index, Residency residency,
                                         std::size_t alignment = 0)
        {
            return context.memory().allocate(
                {usm_kind_for(residency), bytes, alignment, device_index.value()});
        }

        /** @brief Returns the SYCL allocation kind a recorded USM kind is. */
        [[nodiscard]] inline sycl::usm::alloc sycl_kind_of(Internal::Memory::USMKind kind) noexcept
        {
            switch (kind)
            {
                case Internal::Memory::USMKind::Host:   return sycl::usm::alloc::host;
                case Internal::Memory::USMKind::Shared: return sycl::usm::alloc::shared;
                case Internal::Memory::USMKind::Device: return sycl::usm::alloc::device;
            }
            return sycl::usm::alloc::unknown;
        }

        /**
         * @brief Asks SYCL which kind of USM an address is, in the context of a device's queue.
         * @param device The device whose context is asked; nothing asks NUMA node 0's queue.
         * @return The allocation kind, or unknown when that context does not know it.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        [[nodiscard]] inline sycl::usm::alloc usm_kind_of(
            Internal::Execution::RuntimeContext& context, const void* address,
            std::optional<std::size_t> device)
        {
            try
            {
                return sycl::get_pointer_type(
                    address, context.devices().queue_or_numa_local(device, 0).get_context());
            }
            catch (const sycl::exception& error)
            {
                SR_LOG_DEBUG(K_API_LOG_CATEGORY, "get_pointer_type threw for address {}: {}; the allocation kind is "
                             "unknown.", address, error.what());
                return sycl::usm::alloc::unknown;
            }
        }

        /**
         * @brief Reports which kind of USM a handle's home instance is: the kind the directory
         *        recorded at allocation, else what SYCL answers for borrowed memory.
         * @return The allocation kind, or unknown for a handle that is not live or has no
         *         home instance.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        [[nodiscard]] inline sycl::usm::alloc usm_kind_of(
            Internal::Execution::RuntimeContext& context, GlobalHandle handle)
        {
            const Internal::Memory::EntryView view = context.handles().inspect(handle);
            if (view.status != HandleStatus::Ok)
                return sycl::usm::alloc::unknown;
            if (view.home_kind)
                return sycl_kind_of(*view.home_kind);
            const ResolveOutcome home = context.handles().resolve(handle, view.home);
            if (home.status != HandleStatus::Ok)
                return sycl::usm::alloc::unknown;
            return usm_kind_of(context, home.address, Internal::Graph::device_of_space(view.home));
        }
    } // namespace Detail
} // namespace SushiRuntime
