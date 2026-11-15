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

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        namespace
        {
            /** @brief Spaces EntryView::valid_spaces has one bit for. */
            constexpr std::uint32_t K_VALID_SPACE_BITS = 64;

            /** @brief Returns the message of a bind that found no instance of @p handle on @p device. */
            std::string missing_instance_message(GlobalHandle handle, std::size_t device)
            {
                return "KernelPointers::bind(" + std::to_string(device) + "): the buffer of handle slot " +
                       std::to_string(handle.slot()) + " generation " + std::to_string(handle.generation()) +
                       " has no instance on device " + std::to_string(device) +
                       " and its home is not host visible; the coherence plan must copy it there "
                       "before the node runs.";
            }
        } // namespace

        bool home_is_host_visible(const Internal::Memory::EntryView& entry, GlobalHandle handle) noexcept
        {
            if (entry.status != HandleStatus::Ok || entry.home.value() >= K_VALID_SPACE_BITS)
                return false;
            if (((entry.valid_spaces >> entry.home.value()) & 1U) == 0U)
                return false;
            if (entry.home == host_memory_space())
                return true;
            if (entry.home_kind)
                return *entry.home_kind == Internal::Memory::USMKind::Host ||
                       *entry.home_kind == Internal::Memory::USMKind::Shared;
            return has_flags(handle.flags(), HandleFlags::Borrowed);
        }

        KernelPointerSlot make_kernel_pointer_slot(const Internal::Memory::ResidencyView& directory,
                                                   GlobalHandle handle, void* home,
                                                   std::size_t device_count)
        {
            KernelPointerSlot slot;
            slot.handle = handle;
            slot.home = home;
            const std::optional<HandleLocation> location = locate(directory, handle, device_count);
            if (location && location->address == home)
                slot.home_device = location->device;
            slot.host_visible = home_is_host_visible(directory.inspect(handle), handle);
            return slot;
        }

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
                if (instance != nullptr)
                {
                    bound_[index] = instance;
                    continue;
                }
                SR_VERIFY(slot.host_visible, missing_instance_message(slot.handle, device).c_str());
                bound_[index] = slot.home;
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
