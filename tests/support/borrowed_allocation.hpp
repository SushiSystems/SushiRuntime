/**************************************************************************/
/* borrowed_allocation.hpp                                                */
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
 * @file borrowed_allocation.hpp
 * @brief Declares BorrowedAllocation, which holds one borrow of caller memory for a test.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/handle/global_handle.hpp>

namespace TestSupport
{

/**
 * @brief Holds a borrow of a range of caller-owned memory, and with it the range's handle.
 *
 * A graph that borrows the same range while this object lives gets the same handle, so a test
 * can name the memory by handle before it builds the graph.
 */
class BorrowedAllocation
{
    public:
        /**
         * @brief Borrows [address, address + bytes) in the context of @p runtime.
         * @param runtime Must outlive the object.
         */
        BorrowedAllocation(SushiRuntime::Runtime& runtime, void* address, std::size_t bytes)
            : context_(SushiRuntime::Internal::ContextAccess::handle(
                  SushiRuntime::Internal::ContextAccess::shared(runtime)))
        {
            SushiRuntime::Result<SushiRuntime::GlobalHandle> borrowed = SushiRuntime::Detail::borrow_handle(
                context_, address, bytes, 1, 1, SushiRuntime::DeviceIndex{0}, std::nullopt);
            if (borrowed)
                handle_ = *borrowed;
        }

        BorrowedAllocation(const BorrowedAllocation&) = delete;
        BorrowedAllocation& operator=(const BorrowedAllocation&) = delete;

        /** @brief Drops the borrow. */
        ~BorrowedAllocation()
        {
            SushiRuntime::Detail::release_handle(context_, handle_);
        }

        /** @brief Returns the handle of the range; null when the borrow was refused. */
        [[nodiscard]] SushiRuntime::GlobalHandle handle() const noexcept
        {
            return handle_;
        }

    private:
        /// @brief The context the borrow was made in.
        SushiRuntime::ContextHandle context_;

        /// @brief The handle of the borrowed range.
        SushiRuntime::GlobalHandle handle_;
};

} // namespace TestSupport
