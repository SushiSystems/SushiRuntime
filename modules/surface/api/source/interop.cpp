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
#include <utility>

#include <SushiRuntime/api/detail/error_translation.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/context_access.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/memory_exchange.hpp>
#include <SushiRuntime/memory/allocation.hpp>

#include "memory_block.hpp"

namespace SushiRuntime
{
    namespace
    {
        /**
         * @brief Puts an allocation of the engine behind the owner of a block.
         * @return The block, the error allocation holds, or an AllocationFailed error when
         *         the owner cannot be allocated.
         */
        [[nodiscard]] Result<Detail::MemoryBlock>
        owned_block(Result<Internal::Memory::Allocation> allocation)
        {
            if (!allocation)
                return make_error(std::move(allocation.error()));
            Detail::MemoryBlock block;
            Error thrown = Detail::translate_exceptions(
                ErrorCode::AllocationFailed, ErrorCode::AllocationFailed, [&]()
            {
                block = Detail::block_of(std::move(*allocation));
            });
            if (thrown.code != ErrorCode::Ok)
                return make_error(std::move(thrown));
            return block;
        }
    } // namespace

    Result<InteropCapabilities> Interop::capabilities(DeviceIndex device) const
    {
        return Detail::query_capabilities(Detail::ContextAccess::context(context_),
                                          device.value());
    }

    Result<Detail::MemoryBlock> Interop::export_host_block(std::size_t bytes, DeviceIndex device,
                                                           std::size_t alignment) const
    {
        return owned_block(Detail::export_host_memory(
            Detail::ContextAccess::shared(context_), bytes, device.value(), alignment));
    }

    Result<Detail::MemoryBlock>
    Interop::import_device_block(const ExternalMemoryDescriptor& descriptor, std::size_t bytes,
                                 DeviceIndex device) const
    {
        return owned_block(Detail::import_device_memory(
            Detail::ContextAccess::shared(context_), descriptor, bytes, device.value()));
    }
} // namespace SushiRuntime
