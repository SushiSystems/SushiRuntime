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

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
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
} // namespace SushiRuntime
