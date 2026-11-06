/**************************************************************************/
/* compile_pin_policy.cpp                                                 */
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
 * @file compile_pin_policy.cpp
 * @brief Compiles pin_policy.hpp on its own, so the header carries every include it needs.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/machine/pin_policy.hpp>

#include <cstdint>
#include <type_traits>

static_assert(std::is_same_v<std::underlying_type_t<SushiRuntime::PinPolicy>, std::uint8_t>,
              "PinPolicy is stored in one byte");
