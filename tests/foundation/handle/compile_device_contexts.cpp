/**************************************************************************/
/* compile_device_contexts.cpp                                            */
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
 * @file compile_device_contexts.cpp
 * @brief Compiles device_contexts.hpp on its own and writes a consumer against the port alone.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/handle/device_contexts.hpp>

namespace
{
    /** @brief Reads the devices through DeviceContexts, naming no allocator header. */
    [[maybe_unused]] bool consume(const SushiRuntime::DeviceContexts& contexts)
    {
        return contexts.device_count() > 1 && contexts.shares_context(0, 1);
    }
} // namespace
