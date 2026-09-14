/**************************************************************************/
/* usm_allocation_owner.hpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#pragma once

#include <memory>
#include <utility>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/memory/allocation.hpp>

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Frees an address through the runtime context that allocated it.
         * @see docs/design/SYCL_VULKAN_INTEROP.md §3.1 in the SushiEngine repository.
         */
        class UsmAllocationOwner final : public IAllocationOwner
        {
            public:
                /**
                 * @brief Binds the owner to the context that allocated the address.
                 * @param context The context whose free_usm releases the address.
                 */
                explicit UsmAllocationOwner(std::shared_ptr<Execution::RuntimeContext> context) noexcept
                    : context_(std::move(context))
                {
                }

                /**
                 * @brief Frees @p address through the bound context.
                 * @param address The allocation to free.
                 */
                void release(void* address) noexcept override
                {
                    if (context_)
                        context_->free_usm(address);
                }

            private:
                std::shared_ptr<Execution::RuntimeContext> context_;
        };

    } // namespace Memory
} // namespace SushiRuntime
