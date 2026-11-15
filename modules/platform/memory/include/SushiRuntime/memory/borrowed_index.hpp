/**************************************************************************/
/* borrowed_index.hpp                                                     */
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
 * @file borrowed_index.hpp
 * @brief Declares BorrowedIndex, the address-to-handle lookup for memory the caller owns.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>

#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_space.hpp>
#include <SushiRuntime/handle/handle_status.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /** @brief Holds the handle acquire() gave and whether it was minted by that call. */
        struct BorrowedHandle
        {
            GlobalHandle handle; /**< The Borrowed handle that names the range. */
            bool minted = false; /**< True when this call minted it; false when it was live. */
        };

        /**
         * @brief Gives one Borrowed handle per byte range of caller-owned memory.
         *
         * A sorted interval index serves borrowed memory only: the same range gets the same
         * handle while one borrow of it is live, and the last release retires the handle. Calls
         * are made when a buffer is made or dropped, never on a dispatch, and serialise on one
         * mutex.
         */
        class BorrowedIndex final
        {
            public:
                /**
                 * @brief Builds an empty index that mints in @p space.
                 * @param space Must outlive the index.
                 */
                explicit BorrowedIndex(HandleSpace& space) noexcept;

                BorrowedIndex(const BorrowedIndex&) = delete;
                BorrowedIndex& operator=(const BorrowedIndex&) = delete;

                /**
                 * @brief Returns the handle of [address, address + bytes), minting one on the
                 *        first borrow of that range.
                 * @return The handle, InvalidArgument when address is null, bytes is 0 or the
                 *         range overlaps a live range it does not equal, or the error the mint
                 *         failed with.
                 */
                [[nodiscard]] Result<BorrowedHandle> acquire(const void* address, std::size_t bytes);

                /**
                 * @brief Drops one borrow of @p handle and retires it with the last one.
                 * @return Ok, or NullHandle when the index does not hold the handle.
                 */
                HandleStatus release(GlobalHandle handle);

                /** @brief Returns the live handle whose range holds @p address, if any. */
                [[nodiscard]] std::optional<GlobalHandle> find(const void* address) const;

            private:
                /** @brief Holds one borrowed range. */
                struct Interval
                {
                    std::uintptr_t end = 0;  /**< One past the last byte. */
                    GlobalHandle handle;     /**< The handle that names the range. */
                    std::size_t borrows = 0; /**< Live borrows of the range. */
                };

                /// @brief Mints and retires the handles.
                HandleSpace& space_;

                /// @brief Serialises every call.
                mutable std::mutex mutex_;

                /// @brief Live ranges by their first byte.
                std::map<std::uintptr_t, Interval> ranges_;
        };
    } // namespace Internal::Memory
} // namespace SushiRuntime
