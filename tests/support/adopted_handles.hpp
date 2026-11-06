/**************************************************************************/
/* adopted_handles.hpp                                                    */
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
 * @file adopted_handles.hpp
 * @brief Builds a Buffer or a State over a made-up address, so a test reads its keys and releases without a device.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/context/context_handle.hpp>
#include <SushiRuntime/core/allocation.hpp>
#include <SushiRuntime/core/recording_allocation_owner.hpp>

namespace TestSupport
{

/**
 * @brief Returns a distinct non-null address that no test dereferences.
 * @param id The number that names the address; two ids give two addresses.
 */
[[nodiscard]] inline void* made_up_address(std::uintptr_t id)
{
    return reinterpret_cast<void*>(id * 4096u);
}

/**
 * @brief Builds a block at a made-up address whose release is appended to a vector.
 * @param id Names the address, as made_up_address() takes it.
 * @param bytes The size the block claims.
 * @param releases Receives the address when the block is released; outlives the block.
 */
[[nodiscard]] inline SushiRuntime::Detail::MemoryBlock recorded_block(std::uintptr_t id, std::size_t bytes,
                                                                       std::vector<void*>& releases)
{
    return SushiRuntime::Detail::MemoryBlock{SushiRuntime::Detail::Allocation(
        made_up_address(id), bytes, std::make_unique<SushiRuntime::Detail::RecordingAllocationOwner>(releases))};
}

/**
 * @brief Builds a Buffer that adopts a block of @p block_bytes at a made-up address.
 * @tparam Element The element type of the Buffer.
 * @param id Names the address, as made_up_address() takes it.
 * @param count The element count the Buffer claims.
 * @param block_bytes The size of the block it adopts; a block too small for @p count stops the process.
 * @param releases Receives the address when the Buffer releases it; outlives the Buffer.
 */
template <class Element>
[[nodiscard]] SushiRuntime::Buffer<Element> adopted_buffer(std::uintptr_t id, std::size_t count,
                                                           std::size_t block_bytes,
                                                           std::vector<void*>& releases)
{
    return SushiRuntime::Detail::HandleAccess::construct<SushiRuntime::Buffer<Element>>(
        SushiRuntime::ContextHandle(), recorded_block(id, block_bytes, releases), count,
        SushiRuntime::DeviceIndex{0}, SushiRuntime::Residency::Shared);
}

/**
 * @brief Builds a Buffer of @p count elements over a block that holds exactly them.
 * @tparam Element The element type of the Buffer.
 * @param id Names the address, as made_up_address() takes it.
 * @param count The element count of the Buffer.
 * @param releases Receives the address when the Buffer releases it; outlives the Buffer.
 */
template <class Element>
[[nodiscard]] SushiRuntime::Buffer<Element> adopted_buffer(std::uintptr_t id, std::size_t count,
                                                           std::vector<void*>& releases)
{
    return adopted_buffer<Element>(id, count, count * sizeof(Element), releases);
}

/**
 * @brief Builds a State whose two fields are Buffers at two made-up addresses.
 * @tparam Element The element type of the State.
 * @param current_id Names the address of the current field.
 * @param next_id Names the address of the next field.
 * @param count The element count of each field.
 * @param releases Receives each address when its field is released; outlives the State.
 */
template <class Element>
[[nodiscard]] SushiRuntime::State<Element> adopted_state(std::uintptr_t current_id, std::uintptr_t next_id,
                                                         std::size_t count, std::vector<void*>& releases)
{
    return SushiRuntime::Detail::HandleAccess::construct<SushiRuntime::State<Element>>(
        adopted_buffer<Element>(current_id, count, releases),
        adopted_buffer<Element>(next_id, count, releases),
        std::make_shared<SushiRuntime::Detail::StateView<Element>>());
}

} // namespace TestSupport
