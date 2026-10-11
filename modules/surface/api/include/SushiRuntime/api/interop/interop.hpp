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
 * @brief Declares Interop, the Buffer-typed facade over the memory pairing entry points.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <utility>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/interop/host_export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    class Runtime;

    /**
     * @brief Wraps SushiRuntime's SYCL/Vulkan interop primitives in Buffer-typed calls.
     *
     * See docs/design/SYCL_VULKAN_INTEROP.md §3 in the SushiEngine repository.
     */
    class Interop
    {
        public:
            /**
             * @brief Reports what @p device offers a memory pairing.
             * @param device The device queue to inspect.
             * @return The capabilities, or a DeviceLost error when the device does not answer.
             * @pre device names a device of this runtime.
             */
            [[nodiscard]] Result<SushiRuntime::InteropCapabilities>
            capabilities(DeviceIndex device) const
            {
                return SushiRuntime::Detail::query_capabilities(*context_, device.value());
            }

            /**
             * @brief Allocates a host-addressable buffer another API can import.
             * @tparam T          Element type.
             * @param count       Number of elements to export.
             * @param device      The device queue to allocate on.
             * @param alignment   Byte alignment of the returned region.
             * @return The buffer, its address and its rounded byte size, or an AllocationFailed
             *         error when the device returns nothing.
             * @pre alignment is a power of two, and the byte size of count rounded up to it
             *      fits std::size_t.
             */
            template <typename T>
            [[nodiscard]] Result<SushiRuntime::HostExport<T>>
            export_host(std::size_t count, DeviceIndex device, std::size_t alignment) const
            {
                const std::size_t requested =
                    Detail::checked_byte_count("Interop::export_host", count, sizeof(T));
                Result<Internal::Memory::Allocation> allocation =
                    SushiRuntime::Detail::export_host_memory(context_, requested, device.value(),
                                                             alignment);
                if (!allocation)
                    return make_error(std::move(allocation.error()));
                void* address = allocation->address();
                const std::size_t bytes = allocation->bytes();
                return HostExport<T>{
                    Buffer<T>(context_, std::move(*allocation), count, device, Residency::Shared),
                    address, bytes};
            }

            /**
             * @brief Imports a device buffer another API exported.
             * @tparam T        Element type.
             * @param descriptor Names the exported handle; the caller keeps ownership of it.
             * @param count     Number of elements to import.
             * @param device    The device queue to import on.
             * @return A device-resident buffer over the region, empty when @p count is 0, or
             *         why nothing was imported: Unsupported for a device that cannot import,
             *         InvalidAccess for a descriptor the device refuses.
             * @pre The byte size of count fits std::size_t.
             * @see modules/surface/interop/README.md
             */
            template <typename T>
            [[nodiscard]] Result<Buffer<T>>
            import_device(const SushiRuntime::ExternalMemoryDescriptor& descriptor,
                          std::size_t count, DeviceIndex device) const
            {
                const std::size_t requested =
                    Detail::checked_byte_count("Interop::import_device", count, sizeof(T));
                Result<Internal::Memory::Allocation> allocation =
                    SushiRuntime::Detail::import_device_memory(context_, descriptor, requested,
                                                               device.value());
                if (!allocation)
                    return make_error(std::move(allocation.error()));
                return Buffer<T>(context_, std::move(*allocation), count, device,
                                 Residency::Device);
            }

        private:
            friend class Runtime;

            /**
             * @brief Binds the facade to the runtime context it forwards to.
             * @param context Must not be null.
             */
            explicit Interop(std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept
                : context_(std::move(context))
            {
            }

            /** @brief Holds the runtime context the facade forwards to; never null. */
            std::shared_ptr<Internal::Execution::RuntimeContext> context_;
    };
} // namespace SushiRuntime
