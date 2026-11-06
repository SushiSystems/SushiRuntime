/**************************************************************************/
/* descriptor_refusal.cpp                                                 */
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
 * @file descriptor_refusal.cpp
 * @brief Defines the checks that refuse an import descriptor.
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory_exchange/descriptor_refusal.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace SushiRuntime
{
    namespace
    {
        using Internal::MemoryExchange::kind_name;

        /// @brief Spells the members of @p kinds as a comma-separated list, or "no kind".
        std::string kind_names(const ExternalHandleKindSet& kinds)
        {
            std::string text;
            for (const ExternalHandleKind kind : K_NAMED_HANDLE_KINDS)
            {
                if (!kinds.contains(kind))
                    continue;
                if (!text.empty())
                    text += ", ";
                text += kind_name(kind);
            }
            return text.empty() ? std::string("no kind") : text;
        }

        /// @brief Spells @p uuid as thirty-two lowercase hexadecimal digits.
        std::string hex(const DeviceUUID& uuid)
        {
            static constexpr char K_DIGITS[] = "0123456789abcdef";
            std::string text;
            text.reserve(uuid.bytes.size() * 2);
            for (const std::uint8_t byte : uuid.bytes)
            {
                text += K_DIGITS[byte >> 4];
                text += K_DIGITS[byte & 0x0F];
            }
            return text;
        }

        /// @brief Builds the error import_device returns for a descriptor it refuses.
        Error descriptor_error(std::string detail)
        {
            return Error{ErrorCode::InvalidAccess, "import_device: " + std::move(detail)};
        }
    } // namespace

    namespace Internal::MemoryExchange
    {
        const char* kind_name(ExternalHandleKind kind) noexcept
        {
            switch (kind)
            {
                case ExternalHandleKind::Unspecified: return "Unspecified";
                case ExternalHandleKind::OpaqueWin32: return "OpaqueWin32";
                case ExternalHandleKind::OpaqueFd:    return "OpaqueFd";
            }
            return "unknown";
        }

        std::optional<Error> descriptor_refusal(const InteropCapabilities& capabilities,
                                                const ExternalMemoryDescriptor& descriptor,
                                                std::size_t bytes, std::size_t device_index)
        {
            const std::string imported = kind_names(capabilities.importable_kinds);
            if (descriptor.kind == ExternalHandleKind::Unspecified)
                return descriptor_error(
                    std::string("descriptor.kind is Unspecified; the exporter must state ") +
                    "it, and device " + std::to_string(device_index) + " imports " + imported +
                    ".");

            if (!capabilities.importable_kinds.contains(descriptor.kind))
                return descriptor_error(
                    std::string("descriptor.kind is ") + kind_name(descriptor.kind) +
                    " (value " + std::to_string(static_cast<unsigned>(descriptor.kind)) +
                    ") and device " + std::to_string(device_index) + " imports only " +
                    imported + "; export a handle of a kind the device imports.");

            if (descriptor.kind == ExternalHandleKind::OpaqueWin32 && descriptor.handle == 0)
                return descriptor_error(
                    "descriptor.handle is null; set it to the Win32 NT handle the exporting "
                    "API returned.");

            if (descriptor.kind == ExternalHandleKind::OpaqueFd &&
                descriptor.handle > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                return descriptor_error(
                    "descriptor.handle is " + std::to_string(descriptor.handle) +
                    ", above the largest file descriptor, " +
                    std::to_string(std::numeric_limits<int>::max()) +
                    "; set it to the descriptor the exporting API returned.");

            if (descriptor.offset > descriptor.allocation_bytes ||
                bytes > descriptor.allocation_bytes - descriptor.offset)
                return descriptor_error(
                    "the region of " + std::to_string(bytes) + " bytes at offset " +
                    std::to_string(descriptor.offset) + " ends past the allocation of " +
                    std::to_string(descriptor.allocation_bytes) +
                    " bytes that descriptor.allocation_bytes states.");
            return std::nullopt;
        }

        std::optional<Error> device_refusal(const InteropCapabilities& capabilities,
                                            const ExternalMemoryDescriptor& descriptor,
                                            std::size_t device_index)
        {
            if (!capabilities.device_uuid)
                return descriptor_error(
                    "device " + std::to_string(device_index) + " reports no UUID, so it " +
                    "cannot be matched to descriptor.device; import on a device whose " +
                    "capabilities() hold a device_uuid.");

            if (*capabilities.device_uuid != descriptor.device)
                return descriptor_error(
                    "descriptor.device is " + hex(descriptor.device) + " and device " +
                    std::to_string(device_index) + " is " + hex(*capabilities.device_uuid) +
                    "; import on the device that exported the handle.");
            return std::nullopt;
        }
    } // namespace Internal::MemoryExchange
} // namespace SushiRuntime
