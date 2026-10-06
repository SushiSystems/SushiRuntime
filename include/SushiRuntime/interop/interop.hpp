/**************************************************************************/
/* interop.hpp                                                            */
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
 * @file interop.hpp
 * @brief Declares the non-template entry points behind the Interop facade.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/host_export.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        class RuntimeContext;
    }

    namespace Interop
    {
        namespace Detail
        {
            /**
             * @brief Reports what one device offers a memory pairing.
             * @param context      The context whose queue is inspected.
             * @param device_index The device queue to inspect.
             * @return The queried capabilities.
             */
            SUSHIRUNTIME_API InteropCapabilities query_capabilities(
                Execution::RuntimeContext& context, std::size_t device_index);

            /**
             * @brief Allocates host memory another API can import.
             * @return An allocation aligned and rounded to alignment.
             * @throws Core::SushiException With allocation_failed on a non-power-of-two
             *        alignment, or when the allocation fails.
             */
            SUSHIRUNTIME_API Memory::Allocation export_host_memory(
                const std::shared_ptr<Execution::RuntimeContext>& context, std::size_t bytes,
                std::size_t device_index, std::size_t alignment);

            /**
             * @brief Imports device memory another API exported.
             * @param descriptor Names the exported handle and its device.
             * @throws Core::SushiException With unsupported when the device cannot import
             *        external memory, or invalid_access when @p descriptor names a
             *        different device.
             */
            SUSHIRUNTIME_API Memory::Allocation import_device_memory(
                const std::shared_ptr<Execution::RuntimeContext>& context,
                const ExternalMemoryDescriptor& descriptor, std::size_t bytes,
                std::size_t device_index);

        } // namespace Detail
    } // namespace Interop

    namespace API
    {
        /**
         * @brief Provides a fluent seam onto SushiRuntime's SYCL/Vulkan interop primitives.
         *
         * See docs/design/SYCL_VULKAN_INTEROP.md §3 in the SushiEngine repository.
         */
        class Interop
        {
            public:
                /**
                 * @brief Binds the facade to the runtime context it forwards to.
                 * @param context The context whose queues the facade acts on.
                 */
                explicit Interop(std::shared_ptr<Execution::RuntimeContext> context) noexcept
                    : context_(std::move(context))
                {
                }

                /**
                 * @brief Reports what @p device offers a memory pairing.
                 * @param device The device queue to inspect.
                 * @return The queried capabilities.
                 */
                SushiRuntime::Interop::InteropCapabilities capabilities(DeviceIndex device) const
                {
                    return SushiRuntime::Interop::Detail::query_capabilities(*context_, device.value());
                }

                /**
                 * @brief Allocates a host-addressable buffer another API can import.
                 * @tparam T          Element type.
                 * @param count       Number of elements to export.
                 * @param device      The device queue to allocate on.
                 * @param alignment   Byte alignment of the returned region.
                 * @return The buffer, its address and its rounded byte size.
                 */
                template <typename T>
                SushiRuntime::Interop::HostExport<T> export_host(std::size_t count, DeviceIndex device,
                                                                 std::size_t alignment)
                {
                    Memory::Allocation allocation = SushiRuntime::Interop::Detail::export_host_memory(
                        context_, count * sizeof(T), device.value(), alignment);
                    void* address = allocation.address();
                    std::size_t bytes = allocation.bytes();
                    return SushiRuntime::Interop::HostExport<T>{
                        Buffer<T>(context_, std::move(allocation), count, device, Residency::Shared),
                        address, bytes};
                }

                /**
                 * @brief Imports a device buffer another API exported.
                 * @tparam T        Element type.
                 * @param descriptor Names the exported handle and its device.
                 * @param count     Number of elements to import.
                 * @param device    The device queue to import on.
                 * @return A device-resident buffer over the imported region.
                 */
                template <typename T>
                Buffer<T> import_device(const SushiRuntime::Interop::ExternalMemoryDescriptor& descriptor,
                                        std::size_t count, DeviceIndex device)
                {
                    Memory::Allocation allocation = SushiRuntime::Interop::Detail::import_device_memory(
                        context_, descriptor, count * sizeof(T), device.value());
                    return Buffer<T>(context_, std::move(allocation), count, device, Residency::Device);
                }

            private:
                /** @brief Holds the runtime context the facade forwards to. */
                std::shared_ptr<Execution::RuntimeContext> context_;
        };

    } // namespace API
} // namespace SushiRuntime
