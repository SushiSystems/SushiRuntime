/****************************************************************/
/* external_memory.hpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file external_memory.hpp
 * @brief Describes a device allocation exported by one API for another to import.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/interop/device_uuid.hpp>

namespace SushiRuntime
{
    namespace Interop
    {
        /**
         * @brief Names the platform mechanism an external memory handle uses.
         */
        enum class ExternalHandleKind : std::uint8_t
        {
            OpaqueWin32, /**< Names a Win32 NT handle to the allocation. */
            OpaqueFd     /**< Names a POSIX file descriptor to the allocation. */
        };

        /**
         * @brief Describes a device allocation exported by one API for another to import.
         *
         * See docs/design/SYCL_VULKAN_INTEROP.md §3.2 in the SushiEngine repository.
         */
        struct ExternalMemoryDescriptor
        {
            ExternalHandleKind kind = ExternalHandleKind::OpaqueWin32; /**< Names the handle's platform shape. */
            std::uint64_t handle = 0;                                  /**< Holds the exported OS handle, as an opaque integer. */
            std::uint64_t allocation_bytes = 0;                        /**< Holds the size of the allocation the handle refers to. */
            std::uint64_t offset = 0;                                  /**< Holds the byte offset of the imported region within the allocation. */
            DeviceUuid device;                                         /**< Names the device that exported the handle. */
        };

    } // namespace Interop
} // namespace SushiRuntime
