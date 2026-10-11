/**************************************************************************/
/* memory_exchange.hpp                                                    */
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
 * @file memory_exchange.hpp
 * @brief Declares the non-template entry points behind the Interop facade.
 * @see modules/surface/memory_exchange/README.md
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#pragma once

#include <cstddef>
#include <memory>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        class RuntimeContext;
    }

    namespace Detail
    {
        /**
         * @brief Reports what one device offers a memory pairing.
         * @param context      The context whose queue is inspected.
         * @param device_index The device queue to inspect.
         * @return The capabilities, or a DeviceLost error when the device does not answer.
         * @pre device_index names a device of the context.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<InteropCapabilities> query_capabilities(
            Internal::Execution::RuntimeContext& context, std::size_t device_index);

        /**
         * @brief Allocates host memory another API can import.
         * @return An allocation aligned and rounded to alignment, an empty one that
         *         allocates nothing when @p bytes is zero, or an AllocationFailed error when
         *         the device returns nothing.
         * @pre alignment is a power of two, and bytes rounded up to it fits std::size_t.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<Internal::Memory::Allocation> export_host_memory(
            const std::shared_ptr<Internal::Execution::RuntimeContext>& context, std::size_t bytes,
            std::size_t device_index, std::size_t alignment);

        /**
         * @brief Imports device memory another API exported.
         * @param descriptor Names the exported handle; the caller keeps ownership of it.
         * @return The mapped region, an empty allocation that imports nothing when
         *         @p bytes is zero and @p descriptor passes every check, an Unsupported error
         *         when the device cannot import, or an InvalidAccess error when @p descriptor
         *         is refused (see the module README).
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<Internal::Memory::Allocation> import_device_memory(
            const std::shared_ptr<Internal::Execution::RuntimeContext>& context,
            const ExternalMemoryDescriptor& descriptor, std::size_t bytes,
            std::size_t device_index);

    } // namespace Detail
} // namespace SushiRuntime
