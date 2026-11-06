/**************************************************************************/
/* descriptor_refusal.hpp                                                 */
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
 * @file descriptor_refusal.hpp
 * @brief Declares the checks that refuse an import descriptor, pure functions of their inputs.
 * @author Mustafa Garip
 * @see modules/surface/memory_exchange/README.md
 */

#pragma once

#include <cstddef>
#include <optional>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

namespace SushiRuntime
{
    namespace Internal::MemoryExchange
    {
        /**
         * @brief Returns the enumerator name of @p kind.
         * @param kind The kind to name.
         * @return "Unspecified", "OpaqueWin32" or "OpaqueFd", or "unknown" for any other value.
         */
        [[nodiscard]] const char* kind_name(ExternalHandleKind kind) noexcept;

        /**
         * @brief Returns why @p descriptor cannot describe @p bytes on device @p device_index.
         * @param capabilities What the device imports.
         * @param descriptor The descriptor an import names.
         * @param bytes The size of the region imported.
         * @param device_index The device named in the message.
         * @return An InvalidAccess error naming the field at fault, or nothing.
         */
        [[nodiscard]] std::optional<Error> descriptor_refusal(
            const InteropCapabilities& capabilities, const ExternalMemoryDescriptor& descriptor,
            std::size_t bytes, std::size_t device_index);

        /**
         * @brief Returns why device @p device_index is not the one @p descriptor names.
         * @param capabilities What the device reports, its UUID among it.
         * @param descriptor The descriptor an import names.
         * @param device_index The device named in the message.
         * @return An InvalidAccess error naming both UUIDs when both exist, or nothing.
         */
        [[nodiscard]] std::optional<Error> device_refusal(
            const InteropCapabilities& capabilities, const ExternalMemoryDescriptor& descriptor,
            std::size_t device_index);
    } // namespace Internal::MemoryExchange
} // namespace SushiRuntime
