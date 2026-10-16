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
 * @brief Tests USMAllocator's allocation kinds, records, release path and release observer.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/device_probe/device_probe.hpp>

using SushiRuntime::Internal::Memory::USMAllocator;
using SushiRuntime::Internal::Memory::USMKind;
using SushiRuntime::Internal::Memory::USMReleaseObserver;
using SushiRuntime::Internal::Memory::USMRequest;

namespace
{
    /** @brief Holds every kind a request can name, in the order the tests walk them. */
    constexpr std::array<USMKind, 3> K_ALL_KINDS = {USMKind::Host, USMKind::Shared,
                                                    USMKind::Device};

    /** @brief Holds the length of every allocation the tests make, in bytes. */
    constexpr std::size_t K_BYTES = 256;

    /** @brief Holds the byte the tests write through an allocation. */
    constexpr unsigned char K_PATTERN = 0x5A;

    /** @brief Returns the name a failure message prints for a kind. */
    const char* name_of(USMKind kind)
    {
        switch (kind)
        {
            case USMKind::Host:   return "Host";
            case USMKind::Shared: return "Shared";
            case USMKind::Device: return "Device";
        }
        return "unknown";
    }

    /** @brief Returns whether a device can allocate USM of a kind. */
    bool supports(const sycl::device& device, USMKind kind)
    {
        switch (kind)
        {
            case USMKind::Host:   return device.has(sycl::aspect::usm_host_allocations);
            case USMKind::Shared: return device.has(sycl::aspect::usm_shared_allocations);
            case USMKind::Device: return device.has(sycl::aspect::usm_device_allocations);
        }
        return false;
    }

    /** @brief Records every address an allocator reports as released. */
    class RecordingObserver final : public USMReleaseObserver
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

    /** @brief Blocks every on_release() call until the test opens it. */
    class BlockingObserver final : public USMReleaseObserver
    {
        public:
            /** @brief Reports that the call began, then waits for open(). */
            void on_release(void*) noexcept override
            {
                std::unique_lock<std::mutex> lock(mutex_);
                entered_ = true;
                changed_.notify_all();
                changed_.wait(lock, [this] { return opened_; });
            }

            /** @brief Blocks until one on_release() call is waiting. */
            void wait_until_entered()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this] { return entered_; });
            }

            /** @brief Lets every waiting on_release() call return. */
            void open()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                opened_ = true;
                changed_.notify_all();
            }

        private:
            /// @brief Guards the two flags below.
            std::mutex mutex_;
            /// @brief Signals a change of either flag.
            std::condition_variable changed_;
            /// @brief Whether an on_release() call has begun.
            bool entered_ = false;
            /// @brief Whether waiting calls may return.
            bool opened_ = false;
    };

    /** @brief Collects the ERROR lines the logger writes while it is alive. */
    class ErrorLineCapture final
    {
        public:
            /** @brief Makes the logger synchronous and routes its records here. */
            ErrorLineCapture()
                : saved_level_(SushiLog::log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT))
            {
                SushiLog::set_log_synchronous(true);
                SushiLog::set_log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                            SushiLog::LogLevel::Warn);
                SushiLog::set_log_sink(
                    [this](const SushiLog::LogRecord& record)
                    {
                        if (record.level == SushiLog::LogLevel::Error)
                            lines.emplace_back(record.text);
                    });
            }

            /** @brief Restores the default sink and the level the process had. */
            ~ErrorLineCapture()
            {
                SushiLog::set_log_sink(nullptr);
                SushiLog::set_log_level(SushiRuntime::K_SUSHIRUNTIME_LOG_PROJECT,
                                            saved_level_);
            }

            ErrorLineCapture(const ErrorLineCapture&) = delete;
            ErrorLineCapture& operator=(const ErrorLineCapture&) = delete;
            ErrorLineCapture(ErrorLineCapture&&) = delete;
            ErrorLineCapture& operator=(ErrorLineCapture&&) = delete;

            /** @brief ERROR lines seen so far, in order. */
            std::vector<std::string> lines;

        private:
            /// @brief The level the process had before the capture.
            SushiLog::LogLevel saved_level_;
    };

    /** @brief Returns an address as the logger prints it. */
    std::string printed(void* address)
    {
        std::ostringstream stream;
        stream << address;
        return stream.str();
    }

    /** @brief Builds one queue per SYCL device and an allocator over them. */
    class Medium_UsmAllocator : public ::testing::Test
    {
        protected:
            /** @brief Skips when SYCL reports no device; otherwise builds the allocator. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";

                for (const sycl::device& device : sycl::device::get_devices())
                    queues_.emplace_back(device);
                allocator_ = std::make_unique<USMAllocator>(queues_);
            }

            /** @brief Returns the first device that can allocate a kind, or nothing. */
            [[nodiscard]] std::optional<std::size_t> device_supporting(USMKind kind) const
            {
                for (std::size_t index = 0; index < queues_.size(); ++index)
                {
                    if (supports(queues_[index].get_device(), kind))
                        return index;
                }
                return std::nullopt;
            }

            /** @brief Returns the first kind a device can allocate, or nothing. */
            [[nodiscard]] std::optional<USMKind> kind_supported_by(std::size_t device_index) const
            {
                for (const USMKind kind : K_ALL_KINDS)
                {
                    if (supports(queues_[device_index].get_device(), kind))
                        return kind;
                }
                return std::nullopt;
            }

            /** @brief One queue per device; index i is the allocator's device i. */
            std::vector<sycl::queue> queues_;

            /** @brief The allocator under test, built over queues_. */
            std::unique_ptr<USMAllocator> allocator_;
    };
} // namespace

TEST_F(Medium_UsmAllocator, EachKindAllocatesIsUsableIsCountedAndIsReleased)
{
    std::size_t kinds_tested = 0;
    for (const USMKind kind : K_ALL_KINDS)
    {
        const std::optional<std::size_t> device = device_supporting(kind);
        if (!device.has_value())
            continue;
        ++kinds_tested;
        SCOPED_TRACE(name_of(kind));

        const std::size_t baseline = allocator_->live_count();
        void* const address = allocator_->allocate(USMRequest{kind, K_BYTES, 0, *device});
        ASSERT_NE(address, nullptr);
        EXPECT_EQ(allocator_->live_count(), baseline + 1);

        if (kind == USMKind::Device)
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

TEST_F(Medium_UsmAllocator, AnAlignedRequestReturnsAnAddressOnTheAlignment)
{
    std::size_t kinds_tested = 0;
    for (const USMKind kind : K_ALL_KINDS)
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
                    allocator_->allocate(USMRequest{kind, bytes, alignment, *device});
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

TEST_F(Medium_UsmAllocator, AnAllocationResolvesToItsDeviceAtItsBaseAndInside)
{
    const std::size_t device = queues_.size() - 1;
    const std::optional<USMKind> kind = kind_supported_by(device);
    if (!kind.has_value())
        GTEST_SKIP() << "The last SYCL device supports no USM kind.";

    void* const address = allocator_->allocate(USMRequest{*kind, K_BYTES, 0, device});
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

TEST_F(Medium_UsmAllocator, AForeignPointerHasNoDevice)
{
    int foreign = 0;

    EXPECT_FALSE(allocator_->device_of(&foreign).has_value());
    EXPECT_FALSE(allocator_->owning_device(&foreign).has_value());
    EXPECT_FALSE(allocator_->containing_base(&foreign).has_value());

    EXPECT_FALSE(allocator_->device_of(nullptr).has_value());
    EXPECT_FALSE(allocator_->owning_device(nullptr).has_value());
    EXPECT_FALSE(allocator_->containing_base(nullptr).has_value());
}

TEST_F(Medium_UsmAllocator, OwningDeviceAsksSYCLAboutUSMItHasNoRecordOf)
{
    const std::optional<std::size_t> device = device_supporting(USMKind::Shared);
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

TEST_F(Medium_UsmAllocator, AdoptRecordsAnAddressAndForgetDropsIt)
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

TEST_F(Medium_UsmAllocator, ReleaseOfAnAdoptedAddressDropsTheRecordAndDoesNotFreeIt)
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

TEST_F(Medium_UsmAllocator, ASecondReleaseLogsOneErrorWithTheAddressAndFreesNothing)
{
    const std::optional<USMKind> kind = kind_supported_by(0);
    if (!kind.has_value())
        GTEST_SKIP() << "SYCL device 0 supports no USM kind.";

    void* const address = allocator_->allocate(USMRequest{*kind, K_BYTES, 0, 0});
    ASSERT_NE(address, nullptr);
    const std::size_t baseline = allocator_->live_count();

    const ErrorLineCapture capture;
    allocator_->release(address);
    EXPECT_TRUE(capture.lines.empty());
    EXPECT_EQ(allocator_->live_count(), baseline - 1);

    allocator_->release(address);
    EXPECT_EQ(allocator_->live_count(), baseline - 1);
    EXPECT_FALSE(allocator_->device_of(address).has_value());
    ASSERT_EQ(capture.lines.size(), 1u);
    EXPECT_NE(capture.lines[0].find(printed(address)), std::string::npos) << capture.lines[0];
    EXPECT_NE(capture.lines[0].find("no record"), std::string::npos) << capture.lines[0];
}

TEST_F(Medium_UsmAllocator, ReleaseOfForeignUSMLeavesItAllocated)
{
    const std::optional<std::size_t> device = device_supporting(USMKind::Shared);
    if (!device.has_value())
        GTEST_SKIP() << "No SYCL device supports shared USM.";

    // Allocated behind the allocator's back, so it holds no record of the address.
    auto* const bytes =
        static_cast<unsigned char*>(sycl::malloc_shared(K_BYTES, queues_[*device]));
    ASSERT_NE(bytes, nullptr);
    std::memset(bytes, K_PATTERN, K_BYTES);
    const std::size_t baseline = allocator_->live_count();

    {
        const ErrorLineCapture capture;
        allocator_->release(bytes);
        EXPECT_EQ(capture.lines.size(), 1u);
    }
    EXPECT_EQ(allocator_->live_count(), baseline);

    for (std::size_t i = 0; i < K_BYTES; ++i)
        ASSERT_EQ(bytes[i], K_PATTERN) << "byte " << i;
    // A release() that had freed the block would make this the second free of it.
    sycl::free(bytes, queues_[*device]);
}

/** @brief Names the fixture for the tests that stop the process; see README.md. */
using Medium_UsmAllocatorDeathTest = Medium_UsmAllocator;

TEST_F(Medium_UsmAllocatorDeathTest, ABadDeviceIndexStopsWithTheIndexAndTheCount)
{
    const std::size_t count = queues_.size();
    const std::size_t bad_index = count + 7;
    int external = 0;
    const std::string pattern = "USM device index " + std::to_string(bad_index) +
                                " is out of range: the allocator holds " +
                                std::to_string(count) + " device";

    const USMRequest request{USMKind::Shared, 64, 0, bad_index};
    EXPECT_DEATH(static_cast<void>(allocator_->allocate(request)), pattern);
    EXPECT_DEATH(allocator_->adopt(&external, bad_index, sizeof(external)), pattern);
}

TEST_F(Medium_UsmAllocator, TheReleaseObserverHearsEachReleasedAddressExactlyOnce)
{
    const std::optional<USMKind> kind = kind_supported_by(0);
    if (!kind.has_value())
        GTEST_SKIP() << "SYCL device 0 supports no USM kind.";

    RecordingObserver observer;
    allocator_->set_release_observer(&observer);

    void* const first = allocator_->allocate(USMRequest{*kind, K_BYTES, 0, 0});
    void* const second = allocator_->allocate(USMRequest{*kind, K_BYTES, 0, 0});
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

TEST_F(Medium_UsmAllocator, SetReleaseObserverWaitsForAnObserverCallInFlight)
{
    constexpr std::chrono::milliseconds WATCH{200};

    BlockingObserver observer;
    allocator_->set_release_observer(&observer);
    int external = 0;
    allocator_->adopt(&external, 0, sizeof(external));

    std::thread releaser([this, &external] { allocator_->release(&external); });
    observer.wait_until_entered();

    std::atomic<bool> removal_returned{false};
    std::thread remover(
        [this, &removal_returned]
        {
            allocator_->set_release_observer(nullptr);
            removal_returned.store(true);
        });

    // The call is still blocked, so a correct removal has not returned however long this is.
    std::this_thread::sleep_for(WATCH);
    EXPECT_FALSE(removal_returned.load());

    observer.open();
    remover.join();
    releaser.join();
    EXPECT_TRUE(removal_returned.load());
}
