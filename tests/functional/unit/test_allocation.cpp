/****************************************************************/
/* test_allocation.cpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <gtest/gtest.h>
#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/memory/allocation.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

using SushiRuntime::API::Buffer;
using SushiRuntime::API::DeviceIndex;
using SushiRuntime::API::Residency;
using SushiRuntime::Memory::Allocation;
using SushiRuntime::Memory::IAllocationOwner;

namespace
{
    /** @brief Counts the releases it receives and remembers the last address. */
    class CountingOwner final : public IAllocationOwner
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
    /** @brief Builds an Allocation owned by a CountingOwner, for Buffer tests. */
    Allocation counted_allocation(std::uintptr_t address, std::size_t bytes,
                                   int& releases, void*& last)
    {
        return Allocation(fake(address), bytes, std::make_unique<CountingOwner>(releases, last));
    }
}

TEST(Unit_Buffer, AdoptingConstructorExposesTheAdoptedAddress)
{
    int releases = 0;
    void* last = nullptr;
    Buffer<std::uint8_t> buffer(nullptr, counted_allocation(5, 16, releases, last), 16,
                                 DeviceIndex{0}, Residency::Shared);
    EXPECT_EQ(buffer.data(), fake(5));
    EXPECT_EQ(releases, 0);
}

TEST(Unit_Buffer, DestructionReleasesTheAdoptedAllocationOnce)
{
    int releases = 0;
    void* last = nullptr;
    {
        Buffer<std::uint8_t> buffer(nullptr, counted_allocation(6, 16, releases, last), 16,
                                     DeviceIndex{0}, Residency::Shared);
    }
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(6));
}

TEST(Unit_Buffer, AdoptingATooSmallAllocationThrowsAndStillReleasesOnce)
{
    int releases = 0;
    void* last = nullptr;
    EXPECT_THROW(
        {
            Buffer<std::uint32_t> buffer(nullptr, counted_allocation(7, 4, releases, last), 4,
                                          DeviceIndex{0}, Residency::Shared);
        },
        SushiRuntime::Core::SushiException);
    EXPECT_EQ(releases, 1);
    EXPECT_EQ(last, fake(7));
}

TEST(Unit_Buffer, AdoptingACountThatOverflowsItsByteSizeThrows)
{
    int releases = 0;
    void* last = nullptr;
    const std::size_t huge = std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) + 1;
    EXPECT_THROW(
        {
            Buffer<std::uint32_t> buffer(nullptr, counted_allocation(8, 4, releases, last), huge,
                                          DeviceIndex{0}, Residency::Shared);
        },
        SushiRuntime::Core::SushiException);
    EXPECT_EQ(releases, 1);
}
