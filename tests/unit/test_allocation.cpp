/**************************************************************************/
/* test_allocation.cpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_allocation.cpp
 * @brief Tests that Allocation and an adopting Buffer release their address exactly once.
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/memory/allocation.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <memory>
#include <utility>

using SushiRuntime::Buffer;
using SushiRuntime::DeviceIndex;
using SushiRuntime::Residency;
using SushiRuntime::Detail::HandleAccess;
using SushiRuntime::Internal::Memory::Allocation;
using SushiRuntime::Internal::Memory::AllocationOwner;

namespace
{
    /** @brief Counts the releases it receives and remembers the last address. */
    class CountingOwner final : public AllocationOwner
    {
        public:
            /** @brief Binds the owner to the counters it reports into. */
            CountingOwner(int& releases, void*& last) : releases_(releases), last_(last) {}

            /** @brief Records one release of @p address. */
            void release(void* address) noexcept override
            {
                ++releases_;
                last_ = address;
            }

        private:
            int& releases_;
            void*& last_;
    };

    /** @brief Returns a distinct non-null address that is never dereferenced. */
    void* fake(std::uintptr_t n) { return reinterpret_cast<void*>(n * 4096u); }
}

TEST(Unit_Allocation, ReleasesOnceOnDestruction)
{
    int releases = 0;
    void* last = nullptr;
    {
        Allocation allocation(fake(1), 64, std::make_unique<CountingOwner>(releases, last));
        EXPECT_EQ(allocation.address(), fake(1));
        EXPECT_EQ(allocation.bytes(), 64u);
    }
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(1));
}

TEST(Unit_Allocation, MoveTransfersTheReleaseAndEmptiesTheSource)
{
    int releases = 0;
    void* last = nullptr;
    {
        Allocation source(fake(2), 32, std::make_unique<CountingOwner>(releases, last));
        Allocation target(std::move(source));
        EXPECT_TRUE(source.empty());
        EXPECT_EQ(target.address(), fake(2));
    }
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(2));
}

TEST(Unit_Allocation, MoveAssignmentReleasesTheOverwrittenAddressFirst)
{
    int releases = 0;
    void* last = nullptr;
    Allocation first(fake(3), 8, std::make_unique<CountingOwner>(releases, last));
    Allocation second(fake(4), 8, std::make_unique<CountingOwner>(releases, last));
    first = std::move(second);
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(3));
    EXPECT_EQ(first.address(), fake(4));
}

TEST(Unit_Allocation, AnEmptyAllocationHasNoAddressOrOwner)
{
    Allocation allocation;
    EXPECT_TRUE(allocation.empty());
    EXPECT_EQ(allocation.bytes(), 0u);
}

TEST(Unit_Allocation, ResetOnANonNullAddressWithNoOwnerReleasesNothing)
{
    Allocation allocation(fake(9), 16, nullptr);
    EXPECT_NO_THROW(allocation.reset());
    EXPECT_TRUE(allocation.empty());
}

namespace
{
    /** @brief Builds a block whose owner counts its one release, for Buffer tests. */
    SushiRuntime::Detail::MemoryBlock counted_block(std::uintptr_t address, std::size_t bytes,
                                                    int& releases, void*& last)
    {
        SushiRuntime::Detail::MemoryBlock block;
        block.address = fake(address);
        block.bytes = bytes;
        block.owner = std::shared_ptr<void>(fake(address), [&releases, &last](void* released)
        {
            ++releases;
            last = released;
        });
        return block;
    }
}

TEST(Unit_Buffer, AdoptingConstructorExposesTheAdoptedAddress)
{
    int releases = 0;
    void* last = nullptr;
    auto buffer = HandleAccess::construct<Buffer<std::uint8_t>>(
        SushiRuntime::ContextHandle(), counted_block(5, 16, releases, last), 16,
        DeviceIndex{0}, Residency::Shared);
    EXPECT_EQ(buffer.data(), fake(5));
    EXPECT_EQ(releases, 0);
}

TEST(Unit_Buffer, DestructionReleasesTheAdoptedAllocationOnce)
{
    int releases = 0;
    void* last = nullptr;
    {
        auto buffer = HandleAccess::construct<Buffer<std::uint8_t>>(
            SushiRuntime::ContextHandle(), counted_block(6, 16, releases, last), 16,
            DeviceIndex{0}, Residency::Shared);
    }
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(6));
}

TEST(Unit_BufferDeathTest, AdoptingATooSmallAllocationStops)
{
    int releases = 0;
    void* last = nullptr;
    EXPECT_DEATH(
        {
            auto buffer = HandleAccess::construct<Buffer<std::uint32_t>>(
                SushiRuntime::ContextHandle(), counted_block(7, 4, releases, last), 4,
                DeviceIndex{0}, Residency::Shared);
        },
        "Buffer adoption: an allocation of 4 bytes cannot hold 4 elements");
}

TEST(Unit_BufferDeathTest, AdoptingACountThatOverflowsItsByteSizeStops)
{
    int releases = 0;
    void* last = nullptr;
    const std::size_t huge = std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;
    EXPECT_DEATH(
        {
            auto buffer = HandleAccess::construct<Buffer<std::uint32_t>>(
                SushiRuntime::ContextHandle(), counted_block(8, 4, releases, last), huge,
                DeviceIndex{0}, Residency::Shared);
        },
        "Buffer adoption: " + std::to_string(huge) + " elements");
}
