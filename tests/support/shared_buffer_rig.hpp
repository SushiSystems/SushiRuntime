/**************************************************************************/
/* shared_buffer_rig.hpp                                                  */
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
 * @file shared_buffer_rig.hpp
 * @brief Declares SharedBufferRig, a SharedBuffers over a handle table a test fills by address.
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <SushiRuntime/buffer_exchange/shared_buffers.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

#include "handle_table.hpp"

namespace TestSupport
{

/**
 * @brief Gives a test a SharedBuffers whose buffers are host memory it names by address.
 *
 * The rig mints one handle per address in its own handle table, with the instance on device 0,
 * and shares the handle. The address is only the test's way to name memory; the SharedBuffers
 * knows handles.
 */
class SharedBufferRig
{
    public:
        /** @brief Builds a rig with an empty handle table and no shared buffer. */
        SharedBufferRig() : buffers_(table_.resolver(), 1)
        {
        }

        SharedBufferRig(const SharedBufferRig&) = delete;
        SharedBufferRig& operator=(const SharedBufferRig&) = delete;

        /** @brief Returns the shared buffers under test. */
        [[nodiscard]] SushiRuntime::Distributed::SharedBuffers& buffers() noexcept
        {
            return buffers_;
        }

        /** @brief Returns the handle the rig minted for @p address. */
        [[nodiscard]] SushiRuntime::GlobalHandle handle_of(const void* address)
        {
            return table_.place(address);
        }

        /** @brief Shares the memory at @p address, which is @p size_bytes long. */
        SushiRuntime::Distributed::BufferHandle share(void* address, std::uint64_t size_bytes)
        {
            return buffers_.share(table_.place(address), size_bytes);
        }

        /** @brief Stops sharing the memory at @p address; returns the handle it had. */
        SushiRuntime::GlobalHandle unshare(void* address)
        {
            return buffers_.unshare(table_.place(address));
        }

        /** @brief Returns the wire handle of the memory at @p address, or nothing. */
        [[nodiscard]] std::optional<SushiRuntime::Distributed::BufferHandle> lookup(void* address)
        {
            return buffers_.lookup(table_.place(address));
        }

        /** @brief Raises the version of the buffer at @p address, if it is shared. */
        void bump_address(void* address)
        {
            const SushiRuntime::GlobalHandle handle = table_.place(address);
            buffers_.bump_handles(SushiRuntime::span<const SushiRuntime::GlobalHandle>(&handle, 1));
        }

        /** @brief Returns the handle table, for a test that needs a region or its resolver. */
        [[nodiscard]] HandleTable& table() noexcept
        {
            return table_;
        }

    private:
        /// @brief Mints and resolves the handles of the rig.
        HandleTable table_;

        /// @brief The set under test; declared after the table it resolves through.
        SushiRuntime::Distributed::SharedBuffers buffers_;
};

} // namespace TestSupport
