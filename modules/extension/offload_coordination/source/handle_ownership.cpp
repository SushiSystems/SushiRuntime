/**************************************************************************/
/* handle_ownership.cpp                                                   */
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
 * @file handle_ownership.cpp
 * @brief Implements HandleOwnership, which decides the owner machine of a shared handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/handle_ownership.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        OwnershipStatus classify_ownership(GlobalHandle handle, std::uint16_t local_machine) noexcept
        {
            if (handle.is_null())
                return OwnershipStatus::Null;
            if (handle.machine() != local_machine)
                return OwnershipStatus::Foreign;
            return OwnershipStatus::Owned;
        }

    } // namespace Distributed
} // namespace SushiRuntime
