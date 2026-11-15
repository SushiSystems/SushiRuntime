/**************************************************************************/
/* handle_table.hpp                                                       */
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
 * @file handle_table.hpp
 * @brief Mints handles whose instance lies at a test's address on one device, for code that resolves them.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <map>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/handle/handle_status.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace TestSupport
{

/**
 * @brief Mints one handle per address a test places, in its own directory or in a context's.
 *
 * The handle resolves to the address on the device's memory space, as a buffer the runtime
 * made on that device does. Placing one address again returns its handle; the table retires
 * every handle it minted when it is destroyed.
 */
class HandleTable
{
    public:
        /** @brief Mints in a directory of its own. */
        HandleTable() : owned_(std::make_unique<SushiRuntime::Internal::Memory::ResidencyDirectory>(0)),
                        directory_(*owned_)
        {
        }

        /**
         * @brief Mints in @p directory, such as a runtime context's handles().
         * @param directory Must outlive the table.
         */
        explicit HandleTable(SushiRuntime::Internal::Memory::ResidencyDirectory& directory)
            : directory_(directory)
        {
        }

        HandleTable(const HandleTable&) = delete;
        HandleTable& operator=(const HandleTable&) = delete;

        /** @brief Retires every handle the table minted. */
        ~HandleTable()
        {
            for (const auto& placed : placed_)
                static_cast<void>(directory_.retire(placed.second));
        }

        /**
         * @brief Returns the handle whose instance on @p device lies at @p address, minting it
         *        on the first call for the address.
         */
        [[nodiscard]] SushiRuntime::GlobalHandle place(const void* address, std::size_t device = 0)
        {
            const auto found = placed_.find(address);
            if (found != placed_.end())
                return found->second;
            const SushiRuntime::MintOutcome minted =
                directory_.mint(SushiRuntime::HandleKind::Buffer, SushiRuntime::HandleFlags::None);
            EXPECT_EQ(minted.status, SushiRuntime::HandleStatus::Ok);
            EXPECT_EQ(directory_.set_instance(minted.handle,
                                              SushiRuntime::Internal::Graph::device_memory_space(device),
                                              const_cast<void*>(address)),
                      SushiRuntime::HandleStatus::Ok);
            placed_.emplace(address, minted.handle);
            return minted.handle;
        }

        /** @brief Returns the whole region of the handle place() gives @p address. */
        [[nodiscard]] SushiRuntime::BufferRegion region(const void* address, std::size_t device = 0)
        {
            return SushiRuntime::BufferRegion(place(address, device));
        }

        /** @brief Returns the directory, which resolves every handle place() gave. */
        [[nodiscard]] const SushiRuntime::Internal::Memory::ResidencyDirectory& resolver() const noexcept
        {
            return directory_;
        }

    private:
        /// @brief The directory this table made, when it was given none.
        std::unique_ptr<SushiRuntime::Internal::Memory::ResidencyDirectory> owned_;

        /// @brief The directory the handles are minted in.
        SushiRuntime::Internal::Memory::ResidencyDirectory& directory_;

        /// @brief The handle of each placed address.
        std::map<const void*, SushiRuntime::GlobalHandle> placed_;
};

/**
 * @brief Mints in @p directory a handle whose instance on @p device lies at @p address.
 *
 * The handle lives as long as the directory; for a plan that outlives the test's own scope but
 * not the context that owns the directory.
 */
[[nodiscard]] inline SushiRuntime::GlobalHandle place_in(SushiRuntime::Internal::Memory::ResidencyDirectory& directory,
                                                         const void* address, std::size_t device = 0)
{
    const SushiRuntime::MintOutcome minted =
        directory.mint(SushiRuntime::HandleKind::Buffer, SushiRuntime::HandleFlags::None);
    EXPECT_EQ(minted.status, SushiRuntime::HandleStatus::Ok);
    EXPECT_EQ(directory.set_instance(minted.handle, SushiRuntime::Internal::Graph::device_memory_space(device),
                                     const_cast<void*>(address)),
              SushiRuntime::HandleStatus::Ok);
    return minted.handle;
}

/**
 * @brief Returns the whole region named by the handle a process-wide table gives @p address.
 *
 * For a test that names memory it allocated itself; its handles resolve in no runtime, so a
 * node over them runs where it is placed, or on device 0.
 */
[[nodiscard]] inline SushiRuntime::BufferRegion region_at(const void* address)
{
    static HandleTable table;
    return table.region(address);
}

/** @brief Returns the window [offset, offset + length) of the region region_at() gives @p address. */
[[nodiscard]] inline SushiRuntime::BufferRegion region_at(const void* address, std::size_t offset,
                                                          std::size_t length)
{
    return SushiRuntime::BufferRegion(region_at(address).base, offset, length);
}

} // namespace TestSupport
