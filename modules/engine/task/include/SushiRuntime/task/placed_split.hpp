/**************************************************************************/
/* placed_split.hpp                                                       */
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
 * @file placed_split.hpp
 * @brief Declares PlacedSplit, the parts a partitioner cut one node into, each on its device.
 * @see modules/engine/task/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <vector>

#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/split_dispatch.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /** @brief Holds the bytes one part of a split node reads and writes. */
        struct PlacedSplitAccess
        {
            std::vector<BufferRegion> reads;   /**< Bytes the part reads. */
            std::vector<BufferRegion> writes;  /**< Bytes the part writes; the parts of one node write disjoint bytes. */
        };

        /**
         * @brief Holds the parts a node runs as, in slice order, and what each part touches.
         *
         * parts and access have one entry per part; parts is contiguous so dispatch_split() reads it as is.
         */
        struct PlacedSplit
        {
            std::vector<SplitDispatchPart> parts;   /**< Device index and axis range of each part. */
            std::vector<PlacedSplitAccess> access;  /**< Bytes each part reads and writes. */
        };
    } // namespace Internal::Graph
} // namespace SushiRuntime
