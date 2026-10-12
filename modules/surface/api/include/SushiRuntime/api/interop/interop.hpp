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
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

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
            [[nodiscard]] SUSHIRUNTIME_API Result<InteropCapabilities>
            capabilities(DeviceIndex device) const;

            /**
             * @brief Allocates a host-addressable buffer another API can import.
             * @param alignment Byte alignment of the returned region; a power of two.
             * @return The buffer, its address and its rounded byte size, or an AllocationFailed
             *         error when the device or the host allocator returns nothing.
             * @pre The byte size of count, rounded up to alignment, fits std::size_t.
             * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
             */
            template <typename T>
            [[nodiscard]] Result<HostExport<T>>
            export_host(std::size_t count, DeviceIndex device, std::size_t alignment) const
            {
                const std::size_t requested =
                    Detail::checked_byte_count("Interop::export_host", count, sizeof(T));
                Result<Detail::MemoryBlock> block = export_host_block(requested, device, alignment);
                if (!block)
                    return make_error(std::move(block.error()));
                void* address = block->address;
                const std::size_t bytes = block->bytes;
                Result<Buffer<T>> buffer = Buffer<T>::adopt(context_, std::move(*block), count,
                                                            device, Residency::Shared);
                if (!buffer)
                    return make_error(std::move(buffer.error()));
                return HostExport<T>{std::move(*buffer), address, bytes};
            }

            /**
             * @brief Imports a device buffer another API exported.
             * @param descriptor Names the exported handle; the caller keeps ownership of it.
             * @return A device-resident buffer over the region, empty when count is 0, or an
             *         Unsupported, InvalidAccess or AllocationFailed error.
             * @pre The byte size of count fits std::size_t.
             * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
             */
            template <typename T>
            [[nodiscard]] Result<Buffer<T>>
            import_device(const ExternalMemoryDescriptor& descriptor,
                          std::size_t count, DeviceIndex device) const
            {
                const std::size_t requested =
                    Detail::checked_byte_count("Interop::import_device", count, sizeof(T));
                Result<Detail::MemoryBlock> block =
                    import_device_block(descriptor, requested, device);
                if (!block)
                    return make_error(std::move(block.error()));
                return Buffer<T>::adopt(context_, std::move(*block), count, device,
                                        Residency::Device);
            }

        private:
            friend class Runtime;

            /**
             * @brief Binds the facade to the runtime it forwards to.
             * @param context Must not be empty.
             */
            explicit Interop(ContextHandle context) noexcept : context_(std::move(context))
            {
            }

            /**
             * @brief Allocates host memory another API can import.
             * @param bytes Size requested; the block holds it rounded up to alignment.
             * @return The block, or an AllocationFailed error.
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<Detail::MemoryBlock>
            export_host_block(std::size_t bytes, DeviceIndex device, std::size_t alignment) const;

            /**
             * @brief Imports device memory another API exported.
             * @return The block, or why nothing was imported.
             */
            [[nodiscard]] SUSHIRUNTIME_API Result<Detail::MemoryBlock>
            import_device_block(const ExternalMemoryDescriptor& descriptor,
                                std::size_t bytes, DeviceIndex device) const;

            /** @brief The runtime the facade forwards to; never empty. */
            ContextHandle context_;
    };
} // namespace SushiRuntime
