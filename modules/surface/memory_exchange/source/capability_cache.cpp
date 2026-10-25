/**************************************************************************/
/* capability_cache.cpp                                                   */
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
 * @file capability_cache.cpp
 * @brief Defines the probe that asks one SYCL device for its interop capabilities.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#include <SushiRuntime/memory_exchange/capability_cache.hpp>

#include <optional>

#include <sycl/sycl.hpp>

#include <SushiRuntime/interop/device_uuid.hpp>
#include <SushiRuntime/interop/external_memory.hpp>

#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
#include <sycl/ext/oneapi/bindless_images.hpp>
#endif

namespace SushiRuntime::Internal::MemoryExchange
{
    namespace
    {
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
        namespace Experimental = sycl::ext::oneapi::experimental;

        /// @brief Returns the SYCL handle type that stands for @p kind, or nothing for Unspecified.
        std::optional<Experimental::external_mem_handle_type> sycl_handle_type(
            ExternalHandleKind kind) noexcept
        {
            switch (kind)
            {
                case ExternalHandleKind::OpaqueWin32:
                    return Experimental::external_mem_handle_type::win32_nt_handle;
                case ExternalHandleKind::OpaqueFd:
                    return Experimental::external_mem_handle_type::opaque_fd;
                case ExternalHandleKind::Unspecified:
                    return std::nullopt;
            }
            return std::nullopt;
        }
#endif
    } // namespace

    InteropCapabilities probe_device_capabilities(const sycl::device& device)
    {
        InteropCapabilities capabilities;
        capabilities.host_export = device.has(sycl::aspect::usm_host_allocations);
#if defined(SYCL_EXT_INTEL_DEVICE_INFO)
        if (device.has(sycl::aspect::ext_intel_device_info_uuid))
        {
            DeviceUUID uuid;
            uuid.bytes = device.get_info<sycl::ext::intel::info::device::uuid>();
            capabilities.device_uuid = uuid;
        }
#endif
#if defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
        if (capabilities.device_uuid.has_value() &&
            device.has(sycl::aspect::ext_oneapi_external_memory_import))
        {
            for (const ExternalHandleKind kind : K_NAMED_HANDLE_KINDS)
            {
                if (Experimental::supports_importing_handle_type(*sycl_handle_type(kind), device))
                    capabilities.importable_kinds.insert(kind);
            }
            capabilities.device_import = !capabilities.importable_kinds.is_empty();
        }
#endif
        return capabilities;
    }
} // namespace SushiRuntime::Internal::MemoryExchange
