/**************************************************************************/
/* made_up_handle.hpp                                                     */
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
 * @file made_up_handle.hpp
 * @brief Names a buffer by a handle no directory minted, for a test that needs only an identity.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>

#include <SushiRuntime/handle/global_handle.hpp>

namespace TestSupport
{

/**
 * @brief Returns a live-looking handle that no directory minted.
 * @param id The number that names the handle; two ids give two handles.
 */
[[nodiscard]] constexpr SushiRuntime::GlobalHandle made_up_handle(std::uintptr_t id)
{
    return SushiRuntime::GlobalHandle(0, 0, SushiRuntime::HandleKind::Buffer,
                                      SushiRuntime::HandleFlags::None, 1,
                                      static_cast<std::uint32_t>(id));
}

} // namespace TestSupport
