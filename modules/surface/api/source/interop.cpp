/**************************************************************************/
/* interop.cpp                                                            */
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
 * @file interop.cpp
 * @brief Implements the members of Interop that call the interop functions of the engine.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/interop/README.md
 */

#include <SushiRuntime/api/interop/interop.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/capability_cache.hpp>
#include <SushiRuntime/memory_exchange/memory_exchange.hpp>

#include "context_memory.hpp"

namespace SushiRuntime
{
    namespace
    {
        /**
         * @brief Moves an allocation of the engine into the block a Buffer holds.
         * @return The block, or the error allocation holds.
         */
        [[nodiscard]] Result<Detail::MemoryBlock>
        owned_block(Result<Detail::Allocation> allocation)
        {
            if (!allocation)
                return make_error(std::move(allocation.error()));
            return Detail::MemoryBlock{std::move(*allocation)};
        }

        /**
         * @brief Returns the residency a SYCL allocation kind is.
         * @return The residency, or nothing for memory SYCL does not know.
         */
        [[nodiscard]] std::optional<Residency> residency_of(sycl::usm::alloc kind) noexcept
        {
            switch (kind)
            {
                case sycl::usm::alloc::host:   return Residency::Host;
                case sycl::usm::alloc::shared: return Residency::Shared;
                case sycl::usm::alloc::device: return Residency::Device;
                default:                       return std::nullopt;
            }
        }
    } // namespace

    Result<InteropCapabilities> Interop::capabilities(DeviceIndex device) const
    {
        return Internal::MemoryExchange::query_capabilities(
            Internal::ContextAccess::context(context_),
            Internal::ContextAccess::capability_cache(context_), device.value());
    }

    Result<Detail::MemoryBlock> Interop::export_host_block(std::size_t bytes, DeviceIndex device,
                                                           std::size_t alignment) const
    {
        return owned_block(Internal::MemoryExchange::export_host_memory(
            Internal::ContextAccess::shared(context_), bytes, device.value(), alignment));
    }

    Result<Detail::MemoryBlock>
    Interop::import_device_block(const ExternalMemoryDescriptor& descriptor, std::size_t bytes,
                                 DeviceIndex device) const
    {
        return owned_block(Internal::MemoryExchange::import_device_memory(
            Internal::ContextAccess::shared(context_),
            Internal::ContextAccess::capability_cache(context_), descriptor, bytes,
            device.value()));
    }

    Result<void> Interop::require_stated_residency(const void* address, DeviceIndex device,
                                                   Residency stated) const
    {
        const sycl::usm::alloc kind = Detail::usm_kind_of(
            Internal::ContextAccess::context(context_), address, device.value());
        const std::optional<Residency> actual = residency_of(kind);
        if (!actual || *actual == stated)
            return {};
        return make_error(Error{
            ErrorCode::InvalidArgument,
            std::string("Interop::borrow: SYCL knows the memory as Residency::") +
                residency_name(*actual) + " USM, but the call states Residency::" +
                residency_name(stated) + "; pass Residency::" + residency_name(*actual) + "."});
    }
} // namespace SushiRuntime
