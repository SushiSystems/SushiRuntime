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
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

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

        /**
         * @brief Returns the queue of the device that owns an address, or a queue of
         *        NUMA node 0 when no device of the context owns it.
         */
        inline sycl::queue& owning_queue(Internal::Execution::RuntimeContext& context,
                                         const void* address)
        {
            const std::optional<std::size_t> device = context.memory().owning_device(address);
            return context.devices().queue_or_numa_local(device, 0);
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
         * @brief Reports which kind of USM an address is: the kind recorded at allocation,
         *        else what SYCL answers in the context the data README names.
         * @return The allocation kind, or unknown when that context does not know it.
         * @see modules/surface/api/include/SushiRuntime/api/data/README.md
         */
        [[nodiscard]] inline sycl::usm::alloc usm_kind_of(
            Internal::Execution::RuntimeContext& context, const void* address,
            std::optional<std::size_t> stated_device = std::nullopt)
        {
            const std::optional<Internal::Memory::USMRegistry::Record> record =
                context.memory().record_of(address);
            if (record && record->kind)
                return sycl_kind_of(*record->kind);
            try
            {
                std::optional<std::size_t> device = stated_device;
                if (record)
                    device = record->device_index;
                else if (!device)
                    device = context.memory().owning_device(address);
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
    } // namespace Detail
} // namespace SushiRuntime
