/**************************************************************************/
/* usm_allocation_owner.hpp                                               */
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
 * @file usm_allocation_owner.hpp
 * @brief Declares USMAllocationOwner, which releases an address through its USM allocator.
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/memory/allocation.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Releases an address through the allocator that allocated it.
         * @see docs/design/SYCL_VULKAN_INTEROP.md §3.1 in the SushiEngine repository.
         */
        class USMAllocationOwner final : public IAllocationOwner
        {
            public:
                /**
                 * @brief Binds the owner to the allocator that allocated the address.
                 * @param allocator Kept alive by this owner; null makes release() do nothing.
                 */
                explicit USMAllocationOwner(std::shared_ptr<USMAllocator> allocator) noexcept
                    : allocator_(std::move(allocator))
                {
                }

                /**
                 * @brief Releases @p address through the bound allocator.
                 * @param address The allocation to release; USMAllocator::release() throws nothing.
                 */
                void release(void* address) noexcept override
                {
                    if (allocator_)
                        allocator_->release(address);
                }

            private:
                /// @brief Allocator whose release() frees the address.
                std::shared_ptr<USMAllocator> allocator_;
        };

    } // namespace Memory
} // namespace SushiRuntime
