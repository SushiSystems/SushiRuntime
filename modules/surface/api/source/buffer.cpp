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
#include <SushiRuntime/memory/borrowed_index.hpp>
#include <SushiRuntime/memory/handle_ports.hpp>
#include <SushiRuntime/memory/usm_allocation_owner.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/task/handle_location.hpp>

#include "context_memory.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        namespace
        {
            /**
             * @brief Copies bytes on the queue of the device a buffer lives on.
             * @param device The buffer's device, which its handle's home space names.
             * @return The context the copy ran on, or a KernelFailed error when the copy
             *         throws, and an AllocationFailed error when it was refused memory.
             */
            [[nodiscard]] Result<Internal::Execution::RuntimeContext*>
            copy_on_device_queue(const ContextHandle& context, DeviceIndex device,
                                 void* destination, const void* source,
                                 std::size_t bytes) noexcept
            {
                Internal::Execution::RuntimeContext& runtime =
                    Internal::ContextAccess::context(context);
                Result<void> copied = Internal::SYCL::translate_exceptions(
                    [&runtime, device, destination, source, bytes]()
                { runtime.memory().copy(device.value(), destination, source, bytes); });
                if (!copied)
                    return make_error(std::move(copied.error()));
                return &runtime;
            }

            /**
             * @brief Records the size, layout and device instance of a freshly minted handle.
             * @param kind The kind of USM the instance is; nothing for borrowed memory.
             * @return Nothing, or the error the directory refused the entry with.
             */
            [[nodiscard]] Result<void> describe_entry(const Internal::Memory::HandlePorts& ports,
                                                      GlobalHandle handle, void* address,
                                                      std::size_t bytes, std::size_t element_size,
                                                      std::size_t element_alignment,
                                                      std::size_t device,
                                                      std::optional<Internal::Memory::USMKind> kind)
            {
                const MemorySpaceID space = Internal::Graph::device_memory_space(device);
                HandleStatus status = ports.registry.describe(
                    handle, bytes, Internal::Memory::ElementLayout{element_size, element_alignment}, space);
                if (status == HandleStatus::Ok)
                    status = ports.residency.set_instance(handle, space, address, kind);
                if (status != HandleStatus::Ok)
                    return make_error(Error{Internal::Memory::error_code_of(status),
                                            "the residency directory refused the buffer's entry"});
                return {};
            }
        } // namespace

        Result<GlobalHandle> mint_handle(const ContextHandle& context, HandleKind kind,
                                         void* address, std::size_t bytes,
                                         std::size_t element_size, std::size_t element_alignment,
                                         DeviceIndex device, Residency residency)
        {
            if (address == nullptr || !context)
                return GlobalHandle::null();
            const Internal::Memory::HandlePorts ports =
                Internal::ContextAccess::context(context).handle_ports();
            const MintOutcome minted = ports.registry.mint(kind, HandleFlags::None);
            if (minted.status != HandleStatus::Ok)
                return make_error(Error{Internal::Memory::error_code_of(minted.status),
                                        "no handle is left for a new buffer"});
            Result<void> described = describe_entry(ports, minted.handle, address, bytes,
                                                    element_size, element_alignment,
                                                    device.value(), usm_kind_for(residency));
            if (!described)
            {
                static_cast<void>(ports.registry.retire(minted.handle));
                return make_error(std::move(described.error()));
            }
            return minted.handle;
        }

        Result<GlobalHandle> borrow_handle(const ContextHandle& context, void* address,
                                           std::size_t bytes, std::size_t element_size,
                                           std::size_t element_alignment, DeviceIndex device)
        {
            if (address == nullptr || bytes == 0 || !context)
                return GlobalHandle::null();
            Internal::Execution::RuntimeContext& runtime = Internal::ContextAccess::context(context);
            Result<Internal::Memory::BorrowedHandle> borrowed =
                runtime.borrowed_handles().acquire(address, bytes);
            if (!borrowed)
                return make_error(std::move(borrowed.error()));
            if (!borrowed->minted)
                return borrowed->handle;

            Result<void> described = describe_entry(runtime.handle_ports(), borrowed->handle, address,
                                                    bytes, element_size, element_alignment,
                                                    device.value(), std::nullopt);
            if (!described)
            {
                static_cast<void>(runtime.borrowed_handles().release(borrowed->handle));
                return make_error(std::move(described.error()));
            }
            return borrowed->handle;
        }

        void release_handle(const ContextHandle& context, GlobalHandle handle) noexcept
        {
            if (handle.is_null() || !context)
                return;
            Internal::Execution::RuntimeContext& runtime = Internal::ContextAccess::context(context);
            if (has_flags(handle.flags(), HandleFlags::Borrowed))
                static_cast<void>(runtime.borrowed_handles().release(handle));
            else
                static_cast<void>(runtime.handle_ports().registry.retire(handle));
        }

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
                    shared->shared_memory(), device);
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

        Result<void> copy_to_host(const ContextHandle& context, DeviceIndex device,
                                  const void* source, void* destination,
                                  std::size_t bytes) noexcept
        {
            Result<Internal::Execution::RuntimeContext*> copied =
                copy_on_device_queue(context, device, destination, source, bytes);
            if (!copied)
                return make_error(std::move(copied.error()));
            (*copied)->transfers().add_device_to_host(bytes);
            return {};
        }

        Result<void> copy_to_device(const ContextHandle& context, DeviceIndex device,
                                    void* destination, const void* source,
                                    std::size_t bytes) noexcept
        {
            Result<Internal::Execution::RuntimeContext*> copied =
                copy_on_device_queue(context, device, destination, source, bytes);
            if (!copied)
                return make_error(std::move(copied.error()));
            (*copied)->transfers().add_host_to_device(bytes);
            return {};
        }
    } // namespace Detail
} // namespace SushiRuntime
