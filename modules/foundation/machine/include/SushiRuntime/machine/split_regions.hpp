/**************************************************************************/
/* split_regions.hpp                                                      */
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
 * @file split_regions.hpp
 * @brief Declares SplitRegions, the bytes one part of a split node reads and writes.
 * @author Mustafa Garip
 * @see modules/foundation/machine/README.md
 */

#pragma once

#include <vector>

#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    /**
     * @brief Holds the bytes one part of a split node reads and writes, as the consumer states them.
     *
     * A strided slice is several contiguous regions of one buffer. The runtime prices and moves
     * exactly these bytes and reads no layout into them.
     * @see modules/foundation/machine/README.md
     */
    struct SplitRegions
    {
        std::vector<BufferRegion> reads;  /**< Regions the part reads, coalesced by the consumer. */
        std::vector<BufferRegion> writes; /**< Regions the part writes; the parts of one node write disjoint bytes. */
    };
} // namespace SushiRuntime
