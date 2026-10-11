/**************************************************************************/
/* dependencies.hpp                                                       */
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
 * @file dependencies.hpp
 * @brief Declares Reads and Writes, the lists of dependency keys a task names.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
 */

#pragma once

#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/memory/resource_liveness.hpp>
#include <SushiRuntime/memory/resource_region.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        // A State reads through its current buffer and writes through its next buffer.
        template <typename T>
        inline Internal::Memory::ResourceRegion read_key_of(const Buffer<T>& b) noexcept
        {
            return HandleAccess::key(b);
        }
        template <typename T>
        inline Internal::Memory::ResourceRegion read_key_of(const State<T>& s) noexcept
        {
            return HandleAccess::read_key(s);
        }
        inline Internal::Memory::ResourceRegion read_key_of(const Internal::Memory::ResourceRegion& r) noexcept { return r; }
        template <typename T>
        inline Internal::Memory::ResourceRegion write_key_of(const Buffer<T>& b) noexcept
        {
            return HandleAccess::key(b);
        }
        template <typename T>
        inline Internal::Memory::ResourceRegion write_key_of(const State<T>& s) noexcept
        {
            return HandleAccess::write_key(s);
        }
        inline Internal::Memory::ResourceRegion write_key_of(const Internal::Memory::ResourceRegion& r) noexcept { return r; }

        // Only typed handles are watched; a bare ResourceRegion yields an empty observer.
        template <typename T>
        inline Internal::Memory::LivenessObserver liveness_of(const Buffer<T>& b) noexcept
        {
            return HandleAccess::liveness(b);
        }
        template <typename T>
        inline Internal::Memory::LivenessObserver liveness_of(const State<T>& s) noexcept
        {
            return HandleAccess::liveness(s);
        }
        inline Internal::Memory::LivenessObserver liveness_of(const Internal::Memory::ResourceRegion&) noexcept { return {}; }

        /**
         * @brief Appends a handle's observer only when it watches an allocation.
         * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
         */
        template <class Handle>
        inline void gather_life(std::vector<Internal::Memory::LivenessObserver>& out, const Handle& h)
        {
            Internal::Memory::LivenessObserver o = liveness_of(h);
            if (!o.expired())
                out.push_back(std::move(o));
        }
    } // namespace Detail

    /**
     * @brief Names the data a task reads, as a list of dependency keys.
     *
     * Collects the read key of each Buffer, BufferRegion or State passed; add() orders
     * the task after the writers of those keys.
     */
    struct Reads
    {
        std::vector<Internal::Memory::ResourceRegion> keys;  /**< Read regions, in argument order. */
        std::vector<Internal::Memory::LivenessObserver> lives; /**< Observers of typed handles read. */

        /**
         * @brief Collects the read region and liveness of each handle passed.
         * @param hs Buffers, buffer regions, or States the task reads; each
         *           contributes one region, in the order given.
         */
        template <class... Handles>
        explicit Reads(const Handles&... hs) : keys{ Detail::read_key_of(hs)... }
        {
            (Detail::gather_life(lives, hs), ...);
        }
    };

    /**
     * @brief Names the data a task writes, as a list of dependency keys.
     *
     * As Reads, but gathers write keys (a State contributes its next buffer).
     */
    struct Writes
    {
        std::vector<Internal::Memory::ResourceRegion> keys;  /**< Write regions, in argument order. */
        std::vector<Internal::Memory::LivenessObserver> lives; /**< Observers of the typed handles written. */

        /**
         * @brief Collects the write region and liveness of each handle passed.
         * @param hs Buffers, buffer regions, or States the task writes; a State
         *           contributes its next field.
         */
        template <class... Handles>
        explicit Writes(const Handles&... hs) : keys{ Detail::write_key_of(hs)... }
        {
            (Detail::gather_life(lives, hs), ...);
        }
    };



} // namespace SushiRuntime
