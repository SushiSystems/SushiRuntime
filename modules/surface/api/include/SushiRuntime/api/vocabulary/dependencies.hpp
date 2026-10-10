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
    namespace API
    {
        namespace detail
        {
            // A State reads through its current buffer and writes through its next buffer.
            template <typename T>
            inline Core::ResourceRegion read_key_of(const Buffer<T>& b) noexcept
            {
                return HandleAccess::key(b);
            }
            template <typename T>
            inline Core::ResourceRegion read_key_of(const State<T>& s) noexcept
            {
                return HandleAccess::read_key(s);
            }
            inline Core::ResourceRegion read_key_of(const Core::ResourceRegion& r) noexcept { return r; }
            template <typename T>
            inline Core::ResourceRegion write_key_of(const Buffer<T>& b) noexcept
            {
                return HandleAccess::key(b);
            }
            template <typename T>
            inline Core::ResourceRegion write_key_of(const State<T>& s) noexcept
            {
                return HandleAccess::write_key(s);
            }
            inline Core::ResourceRegion write_key_of(const Core::ResourceRegion& r) noexcept { return r; }

            // Only typed handles are watched; a bare ResourceRegion yields an empty observer.
            template <typename T>
            inline Core::LivenessObserver liveness_of(const Buffer<T>& b) noexcept
            {
                return HandleAccess::liveness(b);
            }
            template <typename T>
            inline Core::LivenessObserver liveness_of(const State<T>& s) noexcept
            {
                return HandleAccess::liveness(s);
            }
            inline Core::LivenessObserver liveness_of(const Core::ResourceRegion&) noexcept { return {}; }

            /**
             * @brief Appends a handle's observer only when it watches an allocation.
             * @see modules/surface/api/include/SushiRuntime/api/vocabulary/README.md
             */
            template <class Handle>
            inline void gather_life(std::vector<Core::LivenessObserver>& out, const Handle& h)
            {
                Core::LivenessObserver o = liveness_of(h);
                if (!o.expired())
                    out.push_back(std::move(o));
            }
        } // namespace detail

        /**
         * @brief Names the data a task reads, as a list of dependency keys.
         *
         * The ergonomic counterpart to a hand-built std::vector<void*>: pass the
         * Buffers and States a task reads and this collects their read keys, so the
         * caller never spells a dependency key by hand. Used by the
         * explicit-tracking and offload add() overloads to order tasks by data.
         */
        struct Reads
        {
            std::vector<Core::ResourceRegion> keys;  /**< Read regions, in argument order. */
            std::vector<Core::LivenessObserver> lives; /**< Observers of the typed handles read. */

            /**
             * @brief Collects the read region and liveness of each handle passed.
             * @param hs Buffers, buffer regions, or States the task reads; each
             *           contributes one region, in the order given.
             */
            template <class... Handles>
            explicit Reads(const Handles&... hs) : keys{ detail::read_key_of(hs)... }
            {
                (detail::gather_life(lives, hs), ...);
            }
        };

        /**
         * @brief Names the data a task writes, as a list of dependency keys.
         *
         * As Reads, but gathers write keys (a State contributes its next buffer).
         */
        struct Writes
        {
            std::vector<Core::ResourceRegion> keys;  /**< Write regions, in argument order. */
            std::vector<Core::LivenessObserver> lives; /**< Observers of the typed handles written. */

            /**
             * @brief Collects the write region and liveness of each handle passed.
             * @param hs Buffers, buffer regions, or States the task writes; a State
             *           contributes its next field.
             */
            template <class... Handles>
            explicit Writes(const Handles&... hs) : keys{ detail::write_key_of(hs)... }
            {
                (detail::gather_life(lives, hs), ...);
            }
        };

    } // namespace API
} // namespace SushiRuntime
