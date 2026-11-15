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
 * @brief Implements HandleOwnership, which records the owner machine of each shared handle.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/handle_ownership.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        HandleOwnership::HandleOwnership(std::uint16_t local_machine) noexcept
            : local_machine_(local_machine)
        {

        }

        OwnershipStatus HandleOwnership::admit(GlobalHandle handle)
        {
            if (handle.is_null())
                return OwnershipStatus::Null;
            if (handle.machine() != local_machine_)
                return OwnershipStatus::Foreign;

            const std::lock_guard<std::mutex> lock(mutex_);
            owners_.emplace(handle, handle.machine());
            return OwnershipStatus::Owned;
        }

        void HandleOwnership::forget(GlobalHandle handle) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            owners_.erase(handle);
        }

        std::optional<std::uint16_t> HandleOwnership::owner_of(GlobalHandle handle) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto found = owners_.find(handle);
            if (found == owners_.end())
                return std::nullopt;
            return found->second;
        }

        std::uint16_t HandleOwnership::local_machine() const noexcept
        {
            return local_machine_;
        }

    } // namespace Distributed
} // namespace SushiRuntime
