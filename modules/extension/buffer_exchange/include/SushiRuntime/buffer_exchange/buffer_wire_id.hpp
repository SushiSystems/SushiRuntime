/**************************************************************************/
/* buffer_wire_id.hpp                                                     */
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
 * @file buffer_wire_id.hpp
 * @brief Declares wire_id_of(), the one place a GlobalHandle becomes a wire GlobalBufferID.
 * @author Mustafa Garip
 * @see modules/extension/buffer_exchange/README.md
 */

#pragma once

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Returns the wire id of a handle: its generation and slot, in one word.
         *
         * The id is a pure function of the handle, so no table gives it out. It names one buffer
         * of one master, and a retired slot that is minted again has a new generation and so a
         * new id. Wire version 5 sends the handle itself and this function goes away.
         * @return A valid id for every handle but the null handle, which gets the invalid id.
         */
        [[nodiscard]] constexpr GlobalBufferID wire_id_of(GlobalHandle handle) noexcept
        {
            return GlobalBufferID{handle.to_low_word()};
        }
    } // namespace Distributed
} // namespace SushiRuntime
