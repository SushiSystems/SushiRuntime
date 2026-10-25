/**************************************************************************/
/* buffer.cpp                                                             */
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
 * @file buffer.cpp
 * @brief Implements the calls a Buffer makes into the runtime for its memory.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/data/README.md
 */

#include <SushiRuntime/api/data/buffer.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>

#include "context_memory.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            /**
             * @brief Copies bytes on the queue of the device that owns an allocation.
             * @param base Start of the allocation that holds the device side of the copy.
             * @param device Device whose queue copies when no device of the context owns base.
             * @return The context the copy ran on, or a KernelFailed error when the copy
             *         throws, and an AllocationFailed error when it was refused memory.
             */
            [[nodiscard]] Result<Internal::Execution::RuntimeContext*>
            copy_on_owning_queue(const ContextHandle& context, void* base, DeviceIndex device,
                                 void* destination, const void* source,
                                 std::size_t bytes) noexcept
            {
                Internal::Execution::RuntimeContext& runtime =
                    Internal::ContextAccess::context(context);
                Result<void> copied = Internal::SYCL::translate_exceptions(
                    [&runtime, base, device, destination, source, bytes]()
                {
                    const std::optional<std::size_t> owner = runtime.memory().owning_device(base);
                    runtime.devices()
                        .queue(owner.value_or(device.value()))
                        .memcpy(destination, source, bytes)
                        .wait();
                });
                if (!copied)
                    return make_error(std::move(copied.error()));
                return &runtime;
            }
        } // namespace

        std::size_t device_count(const ContextHandle& context)
        {
            return Internal::ContextAccess::context(context).devices().count();
        }

        Result<MemoryBlock> allocate_block(const ContextHandle& context, std::size_t count,
                                           std::size_t element_size, std::size_t device,
                                           Residency residency, std::size_t alignment)
        {
            const std::size_t bytes = checked_byte_count("Buffer allocation", count, element_size);
            MemoryBlock block;
            std::optional<Error> refused;
            Result<void> allocated = Internal::SYCL::translate_exceptions(
                ErrorCode::AllocationFailed, ErrorCode::AllocationFailed, [&]()
            {
                const std::shared_ptr<Internal::Execution::RuntimeContext> shared =
                    Internal::ContextAccess::shared(context);
                auto owner = std::make_unique<Internal::Memory::USMAllocationOwner>(
                    shared->shared_memory());
                void* address = malloc_residency_on(*shared, bytes, DeviceIndex{device},
                                                    residency, alignment);
                // A null address would give the handle a null key, which skips tracking.
                if (count > 0 && address == nullptr)
                {
                    refused = Error{
                        ErrorCode::AllocationFailed,
                        "Buffer allocation: the device returned no memory for " +
                        describe_request(count, element_size, DeviceIndex{device}, residency) +
                        "."};
                    return;
                }
                block = MemoryBlock{Allocation(address, bytes, std::move(owner))};
            });
            if (!allocated)
                return make_error(std::move(allocated.error()));
            if (refused)
                return make_error(std::move(*refused));
            return block;
        }

        Result<void> copy_to_host(const ContextHandle& context, void* base, DeviceIndex device,
                                  const void* source, void* destination,
                                  std::size_t bytes) noexcept
        {
            Result<Internal::Execution::RuntimeContext*> copied =
                copy_on_owning_queue(context, base, device, destination, source, bytes);
            if (!copied)
                return make_error(std::move(copied.error()));
            (*copied)->transfers().add_device_to_host(bytes);
            return {};
        }

        Result<void> copy_to_device(const ContextHandle& context, void* base, DeviceIndex device,
                                    void* destination, const void* source,
                                    std::size_t bytes) noexcept
        {
            Result<Internal::Execution::RuntimeContext*> copied =
                copy_on_owning_queue(context, base, device, destination, source, bytes);
            if (!copied)
                return make_error(std::move(copied.error()));
            (*copied)->transfers().add_host_to_device(bytes);
            return {};
        }
    } // namespace Detail
} // namespace SushiRuntime
