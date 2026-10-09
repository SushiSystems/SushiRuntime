/**************************************************************************/
/* test_usm_allocator.cpp                                                 */
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
 * @file test_usm_allocator.cpp
 * @brief Tests UsmAllocator's allocation kinds, records, release path and release observer.
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

#include "test_helpers.hpp"

using SushiRuntime::Memory::UsmAllocator;
using SushiRuntime::Memory::UsmKind;
using SushiRuntime::Memory::UsmReleaseObserver;
using SushiRuntime::Memory::UsmRequest;

namespace
{
    /** @brief Holds every kind a request can name, in the order the tests walk them. */
    constexpr std::array<UsmKind, 3> K_ALL_KINDS = {UsmKind::Host, UsmKind::Shared,
                                                    UsmKind::Device};

    /** @brief Holds the length of every allocation the tests make, in bytes. */
    constexpr std::size_t K_BYTES = 256;

    /** @brief Holds the byte the tests write through an allocation. */
    constexpr unsigned char K_PATTERN = 0x5A;

    /** @brief Returns the name a failure message prints for a kind. */
    const char* name_of(UsmKind kind)
    {
        switch (kind)
        {
            case UsmKind::Host:   return "Host";
            case UsmKind::Shared: return "Shared";
            case UsmKind::Device: return "Device";
        }
        return "unknown";
    }

    /** @brief Returns whether a device can allocate USM of a kind. */
    bool supports(const sycl::device& device, UsmKind kind)
    {
        switch (kind)
        {
            case UsmKind::Host:   return device.has(sycl::aspect::usm_host_allocations);
            case UsmKind::Shared: return device.has(sycl::aspect::usm_shared_allocations);
            case UsmKind::Device: return device.has(sycl::aspect::usm_device_allocations);
        }
        return false;
    }

    /** @brief Records every address an allocator reports as released. */
    class RecordingObserver final : public UsmReleaseObserver
    {
        public:
            /** @brief Appends the address; the tests release from one thread. */
            void on_release(void* address) noexcept override
            {
                released.push_back(address);
            }

            /** @brief Addresses reported so far, in call order. */
            std::vector<void*> released;
    };

    /** @brief Builds one queue per SYCL device and an allocator over them. */
    class Unit_UsmAllocator : public ::testing::Test
    {
        protected:
            /** @brief Skips when SYCL reports no device; otherwise builds the allocator. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";

                for (const sycl::device& device : sycl::device::get_devices())
                    queues_.emplace_back(device);
                allocator_ = std::make_unique<UsmAllocator>(queues_);
            }

            /** @brief Returns the first device that can allocate a kind, or nothing. */
            [[nodiscard]] std::optional<std::size_t> device_supporting(UsmKind kind) const
            {
                for (std::size_t index = 0; index < queues_.size(); ++index)
                {
                    if (supports(queues_[index].get_device(), kind))
                        return index;
                }
                return std::nullopt;
            }

            /** @brief Returns the first kind a device can allocate, or nothing. */
            [[nodiscard]] std::optional<UsmKind> kind_supported_by(std::size_t device_index) const
            {
                for (const UsmKind kind : K_ALL_KINDS)
                {
                    if (supports(queues_[device_index].get_device(), kind))
                        return kind;
                }
                return std::nullopt;
            }

            /** @brief One queue per device; index i is the allocator's device i. */
            std::vector<sycl::queue> queues_;

            /** @brief The allocator under test, built over queues_. */
            std::unique_ptr<UsmAllocator> allocator_;
    };
} // namespace

TEST_F(Unit_UsmAllocator, EachKindAllocatesIsUsableIsCountedAndIsReleased)
{
    std::size_t kinds_tested = 0;
    for (const UsmKind kind : K_ALL_KINDS)
    {
        const std::optional<std::size_t> device = device_supporting(kind);
        if (!device.has_value())
            continue;
        ++kinds_tested;
        SCOPED_TRACE(name_of(kind));

        const std::size_t baseline = allocator_->live_count();
        void* const address = allocator_->allocate(UsmRequest{kind, K_BYTES, 0, *device});
        ASSERT_NE(address, nullptr);
        EXPECT_EQ(allocator_->live_count(), baseline + 1);

        if (kind == UsmKind::Device)
        {
            // Device USM has no host address to write through; the device writes it.
            queues_[*device].memset(address, K_PATTERN, K_BYTES).wait_and_throw();
        }
        else
        {
            std::memset(address, K_PATTERN, K_BYTES);
            EXPECT_EQ(static_cast<unsigned char*>(address)[K_BYTES - 1], K_PATTERN);
        }

        allocator_->release(address);
        EXPECT_EQ(allocator_->live_count(), baseline);
        EXPECT_FALSE(allocator_->device_of(address).has_value());
    }

    if (kinds_tested == 0)
        GTEST_SKIP() << "No SYCL device supports any USM kind.";
}

TEST_F(Unit_UsmAllocator, AnAlignedRequestReturnsAnAddressOnTheAlignment)
{
    std::size_t kinds_tested = 0;
    for (const UsmKind kind : K_ALL_KINDS)
    {
        const std::optional<std::size_t> device = device_supporting(kind);
        if (!device.has_value())
            continue;
        ++kinds_tested;

        for (const std::size_t alignment : {std::size_t{64}, std::size_t{128}})
        {
            for (const std::size_t bytes : {std::size_t{1}, std::size_t{100}, std::size_t{4096}})
            {
                SCOPED_TRACE(std::string(name_of(kind)) + ", " + std::to_string(bytes)
                             + " bytes at alignment " + std::to_string(alignment));

                void* const address =
                    allocator_->allocate(UsmRequest{kind, bytes, alignment, *device});
                ASSERT_NE(address, nullptr);
                EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % alignment, 0u);

                const std::optional<std::size_t> recorded = allocator_->device_of(address);
                ASSERT_TRUE(recorded.has_value());
                EXPECT_EQ(*recorded, *device);
                allocator_->release(address);
            }
        }
    }

    if (kinds_tested == 0)
        GTEST_SKIP() << "No SYCL device supports any USM kind.";
}

TEST_F(Unit_UsmAllocator, AnAllocationResolvesToItsDeviceAtItsBaseAndInside)
{
    const std::size_t device = queues_.size() - 1;
    const std::optional<UsmKind> kind = kind_supported_by(device);
    if (!kind.has_value())
        GTEST_SKIP() << "The last SYCL device supports no USM kind.";

    void* const address = allocator_->allocate(UsmRequest{*kind, K_BYTES, 0, device});
    ASSERT_NE(address, nullptr);
    void* const interior = static_cast<unsigned char*>(address) + K_BYTES / 2;
    void* const past_the_end = static_cast<unsigned char*>(address) + K_BYTES;

    const std::optional<std::size_t> recorded = allocator_->device_of(address);
    ASSERT_TRUE(recorded.has_value());
    EXPECT_EQ(*recorded, device);

    const std::optional<std::size_t> owning = allocator_->owning_device(address);
    ASSERT_TRUE(owning.has_value());
    EXPECT_EQ(*owning, device);

    // device_of() answers for a first address only; containing_base() finds it from inside.
    EXPECT_FALSE(allocator_->device_of(interior).has_value());
    const std::optional<void*> base = allocator_->containing_base(interior);
    ASSERT_TRUE(base.has_value());
    EXPECT_EQ(*base, address);
    EXPECT_EQ(allocator_->containing_base(address), std::optional<void*>(address));
    EXPECT_FALSE(allocator_->containing_base(past_the_end).has_value());

    allocator_->release(address);
    EXPECT_FALSE(allocator_->containing_base(interior).has_value());
}

TEST_F(Unit_UsmAllocator, AForeignPointerHasNoDevice)
{
    int foreign = 0;

    EXPECT_FALSE(allocator_->device_of(&foreign).has_value());
    EXPECT_FALSE(allocator_->owning_device(&foreign).has_value());
    EXPECT_FALSE(allocator_->containing_base(&foreign).has_value());

    EXPECT_FALSE(allocator_->device_of(nullptr).has_value());
    EXPECT_FALSE(allocator_->owning_device(nullptr).has_value());
    EXPECT_FALSE(allocator_->containing_base(nullptr).has_value());
}

TEST_F(Unit_UsmAllocator, OwningDeviceAsksSyclAboutUsmItHasNoRecordOf)
{
    const std::optional<std::size_t> device = device_supporting(UsmKind::Shared);
    if (!device.has_value())
        GTEST_SKIP() << "No SYCL device supports shared USM.";

    // Allocated behind the allocator's back, in the context of one of its queues.
    void* const address = sycl::malloc_shared(K_BYTES, queues_[*device]);
    ASSERT_NE(address, nullptr);

    EXPECT_FALSE(allocator_->device_of(address).has_value());
    const std::optional<std::size_t> owning = allocator_->owning_device(address);
    EXPECT_EQ(owning, std::optional<std::size_t>(*device));

    sycl::free(address, queues_[*device]);
}

TEST_F(Unit_UsmAllocator, AdoptRecordsAnAddressAndForgetDropsIt)
{
    const std::size_t baseline = allocator_->live_count();
    const std::size_t device = queues_.size() - 1;
    std::array<int, 8> external{};

    allocator_->adopt(external.data(), device, sizeof(external));
    EXPECT_EQ(allocator_->live_count(), baseline + 1);

    const std::optional<std::size_t> recorded = allocator_->device_of(external.data());
    ASSERT_TRUE(recorded.has_value());
    EXPECT_EQ(*recorded, device);
    EXPECT_EQ(allocator_->containing_base(&external[3]), std::optional<void*>(external.data()));

    allocator_->forget(external.data());
    EXPECT_EQ(allocator_->live_count(), baseline);
    EXPECT_FALSE(allocator_->device_of(external.data()).has_value());

    allocator_->forget(nullptr);
    EXPECT_EQ(allocator_->live_count(), baseline);
}

TEST_F(Unit_UsmAllocator, ReleaseOfAnAdoptedAddressDropsTheRecordAndDoesNotFreeIt)
{
    const std::size_t baseline = allocator_->live_count();
    const auto external = std::make_unique<unsigned char[]>(K_BYTES);
    std::memset(external.get(), K_PATTERN, K_BYTES);

    allocator_->adopt(external.get(), 0, K_BYTES);
    ASSERT_EQ(allocator_->live_count(), baseline + 1);

    // A sycl::free of this heap block would be a free of memory SYCL never allocated.
    allocator_->release(external.get());
    EXPECT_EQ(allocator_->live_count(), baseline);
    EXPECT_FALSE(allocator_->device_of(external.get()).has_value());

    for (std::size_t i = 0; i < K_BYTES; ++i)
        ASSERT_EQ(external[i], K_PATTERN) << "byte " << i;
    external[0] = 0;
    EXPECT_EQ(external[0], 0);
}

TEST_F(Unit_UsmAllocator, ReleaseOfAnAddressWithNoRecordFreesItAgainstDeviceZero)
{
    const std::optional<UsmKind> kind = kind_supported_by(0);
    if (!kind.has_value())
        GTEST_SKIP() << "SYCL device 0 supports no USM kind.";

    void* const address = allocator_->allocate(UsmRequest{*kind, K_BYTES, 0, 0});
    ASSERT_NE(address, nullptr);
    allocator_->forget(address);
    const std::size_t baseline = allocator_->live_count();

    // Device 0 allocated it, so the free against device 0 is the correct one.
    allocator_->release(address);
    EXPECT_EQ(allocator_->live_count(), baseline);
}

namespace
{
/** @brief Expects a call to throw SushiException with invalid_access, an index and a count. */
template <typename Call>
void expect_bad_device_index(Call&& call, std::size_t bad_index, std::size_t count)
{
    try
    {
        call();
        ADD_FAILURE() << "the call did not throw";
    }
    catch (const SushiRuntime::Core::SushiException& exception)
    {
        EXPECT_EQ(exception.error().code, SushiRuntime::Core::Errc::invalid_access);
        const std::string message = exception.what();
        EXPECT_NE(message.find(std::to_string(bad_index)), std::string::npos) << message;
        EXPECT_NE(message.find(std::to_string(count) + " device"), std::string::npos) << message;
    }
}
} // namespace

TEST_F(Unit_UsmAllocator, ABadDeviceIndexThrowsInvalidAccessWithTheIndexAndTheCount)
{
    const std::size_t count = queues_.size();
    const std::size_t bad_index = count + 7;
    const std::size_t baseline = allocator_->live_count();
    int external = 0;

    expect_bad_device_index(
        [&]
        {
            const UsmRequest request{UsmKind::Shared, 64, 0, bad_index};
            static_cast<void>(allocator_->allocate(request));
        },
        bad_index, count);
    expect_bad_device_index(
        [&] { allocator_->adopt(&external, bad_index, sizeof(external)); }, bad_index, count);

    EXPECT_EQ(allocator_->live_count(), baseline);
}

TEST_F(Unit_UsmAllocator, TheReleaseObserverHearsEachReleasedAddressExactlyOnce)
{
    const std::optional<UsmKind> kind = kind_supported_by(0);
    if (!kind.has_value())
        GTEST_SKIP() << "SYCL device 0 supports no USM kind.";

    RecordingObserver observer;
    allocator_->set_release_observer(&observer);

    void* const first = allocator_->allocate(UsmRequest{*kind, K_BYTES, 0, 0});
    void* const second = allocator_->allocate(UsmRequest{*kind, K_BYTES, 0, 0});
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(observer.released.empty());

    allocator_->release(first);
    ASSERT_EQ(observer.released.size(), 1u);
    EXPECT_EQ(observer.released[0], first);

    // Null is ignored and forget() is not a release; neither reaches the observer.
    int external = 0;
    allocator_->release(nullptr);
    allocator_->adopt(&external, 0, sizeof(external));
    allocator_->forget(&external);
    EXPECT_EQ(observer.released.size(), 1u);

    // An adopted address is reported like any other, although it is not freed.
    allocator_->adopt(&external, 0, sizeof(external));
    allocator_->release(&external);
    ASSERT_EQ(observer.released.size(), 2u);
    EXPECT_EQ(observer.released[1], &external);

    allocator_->set_release_observer(nullptr);
    allocator_->release(second);
    EXPECT_EQ(observer.released.size(), 2u);
}
