/**************************************************************************/
/* buffer_range.hpp                                                       */
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

/**
 * @file buffer_range.hpp
 * @brief Declares BufferRange, the bytes [begin, end) of one buffer.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names the bytes [begin, end) of one buffer. */
        struct BufferRange
        {
            GlobalHandle handle;      /**< The buffer. */
            std::uint64_t begin = 0;  /**< First byte. */
            std::uint64_t end = 0;    /**< One past the last byte. */
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
