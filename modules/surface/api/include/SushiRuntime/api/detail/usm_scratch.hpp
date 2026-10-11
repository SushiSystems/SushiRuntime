/**************************************************************************/
/* usm_scratch.hpp                                                        */
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
 * @file usm_scratch.hpp
 * @brief Declares make_usm_scratch, the allocation behind a recorder's scratch.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Allocates USM scratch that returns to its context when the last owner lets go.
         * @param count Elements to allocate; must be non-zero.
         * @throws SushiException If the allocation fails.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        template <typename T>
        inline std::shared_ptr<T> make_usm_scratch(
            std::shared_ptr<Internal::Execution::RuntimeContext> ctx,
            std::size_t count, DeviceIndex device_index, Residency residency)
        {
            const std::size_t bytes = count * sizeof(T);
            void* raw = malloc_residency_on(*ctx, bytes, device_index, residency);

            if (raw == nullptr)
                throw SushiException(Error{
                    Errc::allocation_failed,
                    "Recorder scratch allocation of " + std::to_string(bytes) +
                    " bytes failed (out of USM memory)."});

            return std::shared_ptr<T>(static_cast<T*>(raw),
                                      [ctx](T* p) { ctx->memory().release(p); });
        }
    } // namespace Detail
} // namespace SushiRuntime
