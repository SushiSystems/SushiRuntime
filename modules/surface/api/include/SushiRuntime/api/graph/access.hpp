/**************************************************************************/
/* access.hpp                                                             */
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
 * @file access.hpp
 * @brief Declares Access, what one recorded node reads and writes.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <utility>
#include <vector>

#include <SushiRuntime/api/vocabulary/dependencies.hpp>
#include <SushiRuntime/region/liveness.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the regions a node reads and writes, and watches the handles behind them.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    struct Access
    {
        /** @brief Names no region: the node is ordered against nothing. */
        Access() = default;

        /**
         * @brief Takes the regions and the liveness observers of the handles named.
         * @param read_set  Handles the node reads.
         * @param write_set Handles the node writes.
         */
        Access(const Reads& read_set, const Writes& write_set)
            : reads(read_set.keys), writes(write_set.keys), lives(read_set.lives)
        {
            lives.insert(lives.end(), write_set.lives.begin(), write_set.lives.end());
        }

        /**
         * @brief Takes bare regions; no allocation is watched.
         * @param read_regions  Regions the node reads; the caller keeps them alive.
         * @param write_regions Regions the node writes; the caller keeps them alive.
         */
        Access(std::vector<BufferRegion> read_regions,
               std::vector<BufferRegion> write_regions)
            : reads(std::move(read_regions)), writes(std::move(write_regions))
        {
        }

        std::vector<BufferRegion> reads;    /**< Read regions, in the order given. */
        std::vector<BufferRegion> writes;   /**< Write regions, in the order given. */
        std::vector<LivenessObserver> lives;  /**< Observers a run checks first. */
    };
} // namespace SushiRuntime
