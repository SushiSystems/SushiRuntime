/**************************************************************************/
/* external_memory.hpp                                                    */
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
         * kind has no default; the exporter states it.
         * See docs/design/SYCL_VULKAN_INTEROP.md §3.2 in the SushiEngine repository.
         */
        struct ExternalMemoryDescriptor
        {
            ExternalHandleKind kind;            /**< Names the handle's platform shape. */
            std::uint64_t handle = 0;           /**< Holds the exported OS handle, as an integer. */
            std::uint64_t allocation_bytes = 0; /**< Holds the size of the handle's allocation. */
            std::uint64_t offset = 0;           /**< Holds the region's byte offset in it. */
            DeviceUuid device;                  /**< Names the device that exported the handle. */
        };

    } // namespace Interop
} // namespace SushiRuntime
