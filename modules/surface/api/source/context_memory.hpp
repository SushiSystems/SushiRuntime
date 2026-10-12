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

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
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
                                         void* address)
        {
            const std::optional<std::size_t> device = context.memory().owning_device(address);
            return context.devices().queue_or_numa_local(device, 0);
        }
    } // namespace Detail
} // namespace SushiRuntime
