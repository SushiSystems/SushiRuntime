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
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    namespace API
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
                 * @return The queried capabilities.
                 */
                [[nodiscard]] SushiRuntime::Interop::InteropCapabilities
                capabilities(DeviceIndex device) const
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
                [[nodiscard]] SushiRuntime::Interop::HostExport<T>
                export_host(std::size_t count, DeviceIndex device, std::size_t alignment) const
                {
                    Memory::Allocation allocation = SushiRuntime::Interop::Detail::export_host_memory(
                        context_, count * sizeof(T), device.value(), alignment);
                    void* address = allocation.address();
                    std::size_t bytes = allocation.bytes();
                    return HostExport<T>{
                        Buffer<T>(context_, std::move(allocation), count, device, Residency::Shared),
                        address, bytes};
                }

                /**
                 * @brief Imports a device buffer another API exported.
                 * @tparam T        Element type.
                 * @param descriptor Names the exported handle; the caller keeps ownership of it.
                 * @param count     Number of elements to import.
                 * @param device    The device queue to import on.
                 * @return A device-resident buffer over the region; empty when @p count is 0.
                 */
                template <typename T>
                [[nodiscard]] Buffer<T>
                import_device(const SushiRuntime::Interop::ExternalMemoryDescriptor& descriptor,
                              std::size_t count, DeviceIndex device) const
                {
                    Memory::Allocation allocation = SushiRuntime::Interop::Detail::import_device_memory(
                        context_, descriptor, count * sizeof(T), device.value());
                    return Buffer<T>(context_, std::move(allocation), count, device, Residency::Device);
                }

            private:
                friend class Runtime;

                /**
                 * @brief Binds the facade to the runtime context it forwards to.
                 * @param context Must not be null.
                 */
                explicit Interop(std::shared_ptr<Execution::RuntimeContext> context) noexcept
                    : context_(std::move(context))
                {
                }

                /** @brief Holds the runtime context the facade forwards to; never null. */
                std::shared_ptr<Execution::RuntimeContext> context_;
        };

    } // namespace API
} // namespace SushiRuntime
