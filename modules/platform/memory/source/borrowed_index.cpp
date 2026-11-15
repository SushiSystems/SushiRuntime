/**************************************************************************/
/* borrowed_index.cpp                                                     */
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
 * @file borrowed_index.cpp
 * @brief Implements BorrowedIndex.
 * @see modules/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/memory/borrowed_index.hpp>

#include <iterator>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        BorrowedIndex::BorrowedIndex(HandleSpace& space) noexcept : space_(space)
        {

        }

        Result<BorrowedHandle> BorrowedIndex::acquire(const void* address, std::size_t bytes)
        {
            if (address == nullptr || bytes == 0)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "a borrowed range needs an address and at least one byte"});

            const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
            const std::uintptr_t end = begin + bytes;
            const std::lock_guard<std::mutex> lock(mutex_);

            auto next = ranges_.lower_bound(begin);
            if (next != ranges_.end() && next->first == begin && next->second.end == end)
            {
                ++next->second.borrows;
                return BorrowedHandle{next->second.handle, false};
            }
            const bool overlaps_next = next != ranges_.end() && next->first < end;
            const bool overlaps_previous = next != ranges_.begin() && std::prev(next)->second.end > begin;
            if (overlaps_next || overlaps_previous)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the borrowed range overlaps another borrowed range it does "
                                        "not equal; borrow the whole range once"});

            const MintOutcome minted = space_.mint(HandleKind::Buffer, HandleFlags::Borrowed);
            if (minted.status != HandleStatus::Ok)
                return make_error(Error{error_code_of(minted.status),
                                        "no handle is left for the borrowed range"});
            ranges_.emplace(begin, Interval{end, minted.handle, 1});
            return BorrowedHandle{minted.handle, true};
        }

        HandleStatus BorrowedIndex::release(GlobalHandle handle)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = ranges_.begin(); it != ranges_.end(); ++it)
            {
                if (it->second.handle != handle)
                    continue;
                if (--it->second.borrows != 0)
                    return HandleStatus::Ok;
                ranges_.erase(it);
                return space_.retire(handle);
            }
            return HandleStatus::NullHandle;
        }

        std::optional<GlobalHandle> BorrowedIndex::find(const void* address) const
        {
            const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(address);
            const std::lock_guard<std::mutex> lock(mutex_);
            auto after = ranges_.upper_bound(at);
            if (after == ranges_.begin())
                return std::nullopt;
            const auto& range = *std::prev(after);
            if (at >= range.second.end)
                return std::nullopt;
            return range.second.handle;
        }
    } // namespace Internal::Memory
} // namespace SushiRuntime
