/**************************************************************************/
/* usm_registry.hpp                                                       */
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
 * @file usm_registry.hpp
 * @brief Declares USMRegistry, which records the device each USM pointer was allocated on.
 * @author Mustafa Garip
 * @see modules/platform/memory/README.md
 */

#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <unordered_map>

#include <SushiRuntime/memory/usm_request.hpp>

namespace SushiRuntime
{
    namespace Internal::Memory
    {
        /**
         * @brief Remembers which device each USM pointer was allocated on, and its kind.
         * @see modules/platform/memory/README.md
         */
        class USMRegistry
        {
            public:
                /**
                 * @brief Names the allocation an interior pointer falls inside.
                 *
                 * What containing_allocation() answers with: the address the
                 * allocation starts at, which owner_of() resolves, and the device
                 * that owns it.
                 */
                struct Containment
                {
                    /** @brief First address of the allocation. */
                    const void* base = nullptr;

                    /** @brief Index of the device the allocation belongs to. */
                    std::size_t device_index = 0;
                };

                /** @brief What the registry knows of one allocation at its first address. */
                struct Record
                {
                    /** @brief Index of the device the allocation belongs to. */
                    std::size_t device_index = 0;

                    /** @brief Kind the allocation was made as; nothing for adopted memory. */
                    std::optional<USMKind> kind;
                };

                /**
                 * @brief Records the device, extent and kind of a fresh allocation, replacing a
                 *        known entry.
                 * @param ptr The allocation; null is ignored.
                 * @param bytes Length of the allocation, or nothing when the caller cannot know it.
                 * @param kind Kind it was allocated as, or nothing when another API made it.
                 */
                void record(const void* ptr, std::size_t device_index,
                            std::optional<std::size_t> bytes, std::optional<USMKind> kind)
                {
                    if (!ptr)
                        return;
                    std::lock_guard<std::mutex> lock(mutex_);
                    owners_[ptr] = Entry{device_index, bytes, kind};
                }

                /**
                 * @brief Looks up the device and kind of a first address, in constant time.
                 * @return The record, or nothing for null and for an address with no entry.
                 */
                std::optional<Record> record_of(const void* ptr) const
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = owners_.find(ptr);
                    if (it == owners_.end())
                        return std::nullopt;
                    return Record{it->second.device_index, it->second.kind};
                }

                /**
                 * @brief Looks up the device a pointer was allocated on.
                 *
                 * @param ptr The allocation to resolve. Null resolves to nothing.
                 * @return The recorded device index, or nothing when the pointer was
                 *         not allocated through this registry — which a caller must
                 *         treat as "unknown", not as device zero.
                 */
                std::optional<std::size_t> owner_of(const void* ptr) const
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = owners_.find(ptr);
                    return it == owners_.end() ? std::nullopt
                                               : std::optional<std::size_t>(it->second.device_index);
                }

                /**
                 * @brief Finds the recorded allocation a pointer falls inside, by a linear scan.
                 * @param ptr Any address, interior or base; null resolves to nothing.
                 * @return The allocation covering ptr, or nothing when none does.
                 * @see modules/platform/memory/README.md
                 */
                std::optional<Containment> containing_allocation(const void* ptr) const
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    for (const auto& entry : owners_)
                    {
                        if (entry.first == ptr)
                            return Containment{entry.first, entry.second.device_index};
                        if (!entry.second.bytes.has_value())
                            continue;
                        auto* const base = static_cast<const unsigned char*>(entry.first);
                        auto* const address = static_cast<const unsigned char*>(ptr);
                        if (address > base && address < base + *entry.second.bytes)
                            return Containment{entry.first, entry.second.device_index};
                    }
                    return std::nullopt;
                }

                /**
                 * @brief Resolves a pointer's device and forgets it under one lock.
                 * @return The device index the pointer was recorded under, or nothing if it was
                 *        unknown.
                 * @see modules/platform/memory/README.md
                 */
                std::optional<std::size_t> take(const void* ptr)
                {
                    if (!ptr)
                        return std::nullopt;
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto it = owners_.find(ptr);
                    if (it == owners_.end())
                        return std::nullopt;
                    const std::size_t owner = it->second.device_index;
                    owners_.erase(it);
                    return owner;
                }

                /**
                 * @brief Number of allocations currently recorded.
                 *
                 * @return Live entries: pointers recorded and not yet taken. A
                 *         non-zero count at teardown means an allocation outlived
                 *         its runtime.
                 */
                std::size_t size() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return owners_.size();
                }

            private:
                /** @brief What the table holds for one recorded allocation. */
                struct Entry
                {
                    /** @brief Index of the device the allocation belongs to. */
                    std::size_t device_index = 0;

                    /** @brief Length in bytes, or nothing when the caller had none. */
                    std::optional<std::size_t> bytes;

                    /** @brief Kind the allocation was made as, or nothing for adopted memory. */
                    std::optional<USMKind> kind;
                };

                /** @brief Guards owners_. */
                mutable std::mutex mutex_;

                /** @brief One entry per recorded first address. */
                std::unordered_map<const void*, Entry> owners_;
        };

    } // namespace Memory
} // namespace SushiRuntime
