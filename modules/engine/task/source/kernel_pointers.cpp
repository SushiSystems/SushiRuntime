/**************************************************************************/
/* kernel_pointers.cpp                                                    */
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
 * @file kernel_pointers.cpp
 * @brief Defines the rebinding of a node's kernel pointers and the thread-local scope slot.
 * @author Mustafa Garip
 * @see modules/engine/task/README.md
 */

#include <SushiRuntime/task/kernel_pointers.hpp>

#include <utility>

#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        KernelPointers::KernelPointers(std::vector<KernelPointerSlot> slots,
                                       const HandleResolver& resolver)
            : slots_(std::move(slots)), bound_(slots_.size(), nullptr), resolver_(&resolver)
        {
            for (std::size_t index = 0; index < slots_.size(); ++index)
                bound_[index] = slots_[index].home;
        }

        void KernelPointers::bind(std::size_t device) noexcept
        {
            bool resolved = false;
            for (std::size_t index = 0; index < slots_.size(); ++index)
            {
                const KernelPointerSlot& slot = slots_[index];
                if (slot.home_device == device)
                {
                    bound_[index] = slot.home;
                    continue;
                }
                resolved = true;
                void* const instance = resolve_on_device(*resolver_, slot.handle, device);
                bound_[index] = instance != nullptr ? instance : slot.home;
            }
            if (resolved)
                ++resolved_binds_;
            else
                ++home_binds_;
        }

        const KernelPointers*& KernelPointerScope::slot() noexcept
        {
            static thread_local const KernelPointers* open = nullptr;
            return open;
        }
    } // namespace Internal::Graph
} // namespace SushiRuntime
