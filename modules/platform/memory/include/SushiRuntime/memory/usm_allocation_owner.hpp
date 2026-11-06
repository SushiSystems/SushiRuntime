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
 * @file usm_allocation_owner.hpp
 * @brief Declares USMAllocationOwner, which releases an address through its USM allocator.
 * @author Mustafa Garip
 */

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/core/allocation.hpp>

#include <SushiRuntime/memory/usm_allocator.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Releases an address through the allocator that allocated it.
         * @see docs/design/SYCL_VULKAN_INTEROP/README.md §3.1 in the SushiEngine repository.
         */
        class USMAllocationOwner final : public Detail::AllocationOwner
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
