/****************************************************************/
/* usm_allocation_owner.hpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file usm_allocation_owner.hpp
 * @brief Declares UsmAllocationOwner, which frees an address through its runtime context.
 * @author Mustafa Garip
 */

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
