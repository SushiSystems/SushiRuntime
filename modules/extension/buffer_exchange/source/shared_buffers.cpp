/**************************************************************************/
/* shared_buffers.cpp                                                     */
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
 * @file shared_buffers.cpp
 * @brief Implements SharedBuffers.
 * @author Mustafa Garip
 * @see modules/extension/buffer_exchange/README.md
 */

#include <SushiRuntime/buffer_exchange/shared_buffers.hpp>

#include <cstring>

#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /**
             * @brief Returns whether a buffer at @p current admits a copy of @p length bytes
             *        at @p offset for a caller that names @p version.
             * @return Ok, Stale or OutOfRange; never Gone.
             */
            BufferAccessStatus admit_access(const BufferHandle& current, std::uint32_t version,
                                            std::uint64_t offset, std::size_t length) noexcept
            {
                if (current.version != version)
                    return BufferAccessStatus::Stale;

                if (offset > current.size_bytes || length > current.size_bytes - offset)
                    return BufferAccessStatus::OutOfRange;

                return BufferAccessStatus::Ok;
            }
        } // namespace

        SharedBuffers::SharedBuffers(const HandleResolver& resolver, std::size_t device_count) noexcept
            : resolver_(resolver), device_count_(device_count)
        {

        }

        SharedBuffers::~SharedBuffers() = default;

        void* SharedBuffers::address_of(const Entry& entry) const noexcept
        {
            const std::optional<Internal::Graph::HandleLocation> location =
                Internal::Graph::locate(resolver_, entry.wire.id, device_count_);
            return location ? location->address : nullptr;
        }

        BufferHandle SharedBuffers::share(GlobalHandle handle, std::uint64_t size_bytes)
        {
            if (handle.is_null())
                return BufferHandle{};

            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = by_handle_.find(handle);
            if (known != by_handle_.end())
                return known->second.wire;

            Entry entry;
            entry.wire.id = handle;
            entry.wire.version = 1;
            entry.wire.size_bytes = size_bytes;
            by_handle_.emplace(handle, entry);
            return entry.wire;
        }

        GlobalHandle SharedBuffers::unshare(GlobalHandle handle) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = by_handle_.find(handle);
            if (known == by_handle_.end())
                return GlobalHandle::null();

            by_handle_.erase(known);
            return handle;
        }

        std::optional<BufferHandle> SharedBuffers::lookup(GlobalHandle handle) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = by_handle_.find(handle);
            if (known == by_handle_.end())
                return std::nullopt;

            return known->second.wire;
        }

        bool SharedBuffers::resolve(SushiRuntime::span<const GlobalHandle> handles,
                                    std::vector<BufferHandle>& out) const
        {
            out.clear();
            out.reserve(handles.size());

            std::lock_guard<std::mutex> lock(mutex_);
            for (const GlobalHandle handle : handles)
            {
                const auto known = by_handle_.find(handle);
                if (known == by_handle_.end())
                    return false;

                out.push_back(known->second.wire);
            }
            return true;
        }

        bool SharedBuffers::refresh(SushiRuntime::span<BufferHandle> handles) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (BufferHandle& handle : handles)
            {
                const auto entry = by_handle_.find(handle.id);
                if (entry == by_handle_.end())
                    return false;

                handle.version = entry->second.wire.version;
            }
            return true;
        }

        void SharedBuffers::bump(SushiRuntime::span<BufferHandle> handles) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (BufferHandle& handle : handles)
            {
                const auto entry = by_handle_.find(handle.id);
                if (entry == by_handle_.end())
                    continue;

                handle.version = ++entry->second.wire.version;
            }
        }

        void SharedBuffers::bump_handles(SushiRuntime::span<const GlobalHandle> handles) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const GlobalHandle handle : handles)
            {
                const auto known = by_handle_.find(handle);
                if (known == by_handle_.end())
                    continue;

                ++known->second.wire.version;
            }
        }

        BufferAccessStatus SharedBuffers::read(GlobalHandle id, std::uint32_t version,
                                               std::uint64_t offset,
                                               SushiRuntime::span<std::byte> out) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto entry = by_handle_.find(id);
            if (entry == by_handle_.end())
                return BufferAccessStatus::Gone;

            const BufferAccessStatus status =
                admit_access(entry->second.wire, version, offset, out.size());
            if (status != BufferAccessStatus::Ok || out.empty())
                return status;

            const auto* base = static_cast<const std::byte*>(address_of(entry->second));
            if (base == nullptr)
                return BufferAccessStatus::Gone;

            std::memcpy(out.data(), base + offset, out.size());
            return BufferAccessStatus::Ok;
        }

        BufferAccessStatus SharedBuffers::write(GlobalHandle id, std::uint32_t version,
                                                std::uint64_t offset,
                                                SushiRuntime::span<const std::byte> in)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto entry = by_handle_.find(id);
            if (entry == by_handle_.end())
                return BufferAccessStatus::Gone;

            const BufferAccessStatus status =
                admit_access(entry->second.wire, version, offset, in.size());
            if (status != BufferAccessStatus::Ok || in.empty())
                return status;

            auto* base = static_cast<std::byte*>(address_of(entry->second));
            if (base == nullptr)
                return BufferAccessStatus::Gone;

            std::memcpy(base + offset, in.data(), in.size());
            return BufferAccessStatus::Ok;
        }

    } // namespace Distributed
} // namespace SushiRuntime
