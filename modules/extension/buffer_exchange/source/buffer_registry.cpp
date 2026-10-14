/**************************************************************************/
/* buffer_registry.cpp                                                    */
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
 * @file buffer_registry.cpp
 * @brief Keeps the ids and versions of shared buffers and copies their bytes under one lock.
 * @author Mustafa Garip
 * @see modules/extension/buffer_exchange/README.md
 */

#include <SushiRuntime/buffer_exchange/buffer_registry.hpp>

#include <cstring>
#include <new>

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

        BufferRegistry::BufferRegistry() = default;

        BufferRegistry::~BufferRegistry() = default;

        BufferHandle BufferRegistry::register_buffer(void* address, std::uint64_t size_bytes)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = id_by_address_.find(address);
            if (known != id_by_address_.end())
                return by_id_.find(known->second)->second.handle;

            Entry entry;
            entry.address = address;
            entry.handle.id.value = next_id_;
            entry.handle.version = 1;
            entry.handle.size_bytes = size_bytes;

            // Every address in id_by_address_ has its entry in by_id_; a half insert is undone.
            by_id_.emplace(next_id_, entry);

            /** @brief Erases the entry of an id unless the address was recorded too. */
            struct HalfInsert
            {
                std::unordered_map<std::uint64_t, Entry>& entries; /**< The map to undo in. */
                std::uint64_t id;                                  /**< The entry's id. */
                bool is_complete = false;                          /**< Set once both maps hold it. */

                /** @brief Erases the entry when the second insert did not finish. */
                ~HalfInsert()
                {
                    if (!is_complete)
                        entries.erase(id);
                }
            } half_insert{by_id_, next_id_};

            id_by_address_.emplace(address, next_id_);
            half_insert.is_complete = true;
            ++next_id_;
            return entry.handle;
        }

        GlobalBufferID BufferRegistry::unregister_buffer(void* address) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = id_by_address_.find(address);
            if (known == id_by_address_.end())
                return GlobalBufferID{};

            const GlobalBufferID id{known->second};
            by_id_.erase(known->second);
            id_by_address_.erase(known);
            return id;
        }

        std::optional<BufferHandle> BufferRegistry::lookup(void* address) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto known = id_by_address_.find(address);
            if (known == id_by_address_.end())
                return std::nullopt;

            return by_id_.find(known->second)->second.handle;
        }

        std::optional<BufferHandle> BufferRegistry::lookup_by_id(GlobalBufferID id) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto entry = by_id_.find(id.value);
            if (entry == by_id_.end())
                return std::nullopt;

            return entry->second.handle;
        }

        bool BufferRegistry::resolve(SushiRuntime::span<void* const> addresses,
                                     std::vector<BufferHandle>& out) const
        {
            out.clear();
            out.reserve(addresses.size());

            std::lock_guard<std::mutex> lock(mutex_);
            for (void* address : addresses)
            {
                const auto known = id_by_address_.find(address);
                if (known == id_by_address_.end())
                    return false;

                out.push_back(by_id_.find(known->second)->second.handle);
            }
            return true;
        }

        bool BufferRegistry::refresh(SushiRuntime::span<BufferHandle> handles) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (BufferHandle& handle : handles)
            {
                const auto entry = by_id_.find(handle.id.value);
                if (entry == by_id_.end())
                    return false;

                handle.version = entry->second.handle.version;
            }
            return true;
        }

        void BufferRegistry::bump(SushiRuntime::span<BufferHandle> handles) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (BufferHandle& handle : handles)
            {
                const auto entry = by_id_.find(handle.id.value);
                if (entry == by_id_.end())
                    continue;

                handle.version = ++entry->second.handle.version;
            }
        }

        void BufferRegistry::bump_addresses(SushiRuntime::span<void* const> addresses) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (void* address : addresses)
            {
                const auto known = id_by_address_.find(address);
                if (known == id_by_address_.end())
                    continue;

                ++by_id_.find(known->second)->second.handle.version;
            }
        }

        BufferAccessStatus BufferRegistry::read(GlobalBufferID id, std::uint32_t version,
                                                std::uint64_t offset,
                                                SushiRuntime::span<std::byte> out) const
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto entry = by_id_.find(id.value);
            if (entry == by_id_.end())
                return BufferAccessStatus::Gone;

            const BufferAccessStatus status =
                admit_access(entry->second.handle, version, offset, out.size());
            if (status != BufferAccessStatus::Ok || out.empty())
                return status;

            const auto* base = static_cast<const std::byte*>(entry->second.address);
            std::memcpy(out.data(), base + offset, out.size());
            return BufferAccessStatus::Ok;
        }

        BufferAccessStatus BufferRegistry::write(GlobalBufferID id, std::uint32_t version,
                                                 std::uint64_t offset,
                                                 SushiRuntime::span<const std::byte> in)
        {
            std::lock_guard<std::mutex> lock(mutex_);

            const auto entry = by_id_.find(id.value);
            if (entry == by_id_.end())
                return BufferAccessStatus::Gone;

            const BufferAccessStatus status =
                admit_access(entry->second.handle, version, offset, in.size());
            if (status != BufferAccessStatus::Ok || in.empty())
                return status;

            auto* base = static_cast<std::byte*>(entry->second.address);
            std::memcpy(base + offset, in.data(), in.size());
            return BufferAccessStatus::Ok;
        }

    } // namespace Distributed
} // namespace SushiRuntime
