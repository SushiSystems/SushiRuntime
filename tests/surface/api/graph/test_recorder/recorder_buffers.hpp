/**************************************************************************/
/* recorder_buffers.hpp                                                   */
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
 * @file recorder_buffers.hpp
 * @brief Declares the zeroed word buffer the semantics and parameter-limit tests of the Recorder start from.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <SushiRuntime/sushiruntime.hpp>

namespace RecorderSupport
{
    /** @brief Words in each buffer that zeroed() allocates. */
    constexpr std::size_t K_WIDTH = 32;

    /** @brief Allocates K_WIDTH words and zeroes them. */
    inline SushiRuntime::Buffer<std::uint32_t> zeroed(const SushiRuntime::Runtime& rt)
    {
        auto buffer = rt.buffer<std::uint32_t>(K_WIDTH).value();
        for (std::size_t i = 0; i < K_WIDTH; ++i)
            buffer[i] = 0;
        return buffer;
    }
} // namespace RecorderSupport
