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
 * @file test_usm_allocator.cpp
 * @brief Tests USMAllocator's allocation kinds, records, release path and release observer.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/recording_usm_release_observer.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_release_observer.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

#include "support/runtime_environment.hpp"

using SushiRuntime::Internal::Memory::make_host_usm_source;
using SushiRuntime::Internal::Memory::RecordingUSMReleaseObserver;
using SushiRuntime::Internal::Memory::USMAllocator;
using SushiRuntime::Internal::Memory::USMKind;
using SushiRuntime::Internal::Memory::USMReleaseObserver;
using SushiRuntime::Internal::Memory::USMRequest;

namespace
{
    /** @brief Holds the length of every allocation the tests make, in bytes. */
    constexpr std::size_t K_BYTES = 256;

    /** @brief Holds the byte the tests write through an allocation. */
    constexpr unsigned char K_PATTERN = 0x5A;

    /** @brief Holds the number of devices of the host source the small tests allocate from. */
    constexpr std::size_t K_HOST_DEVICES = 2;

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

    /** @brief Blocks every on_release() call until the test opens it. */
    class BlockingObserver final : public USMReleaseObserver
    {
        public:
            /** @brief Reports that the call began, waits for open(), then reports it finished. */
            void on_release(void*) noexcept override
            {
                std::unique_lock<std::mutex> lock(mutex_);
                entered_ = true;
                changed_.notify_all();
                changed_.wait(lock, [this] { return opened_; });
                finished_ = true;
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

            /** @brief Returns whether an on_release() call ran to its end. */
            [[nodiscard]] bool has_finished()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return finished_;
            }

        private:
            /// @brief Guards the three flags below.
            std::mutex mutex_;
            /// @brief Signals a change of any flag.
            std::condition_variable changed_;
            /// @brief Whether an on_release() call has begun.
            bool entered_ = false;
            /// @brief Whether waiting calls may return.
            bool opened_ = false;
            /// @brief Whether an on_release() call has run to its end.
            bool finished_ = false;
    };

    /** @brief Returns when @p flag is set, giving the CPU to the other threads meanwhile. */
    void wait_until_set(const std::atomic<bool>& flag)
    {
        while (!flag.load())
            std::this_thread::yield();
    }

    /** @brief Returns an address as the logger prints it. */
    std::string printed(void* address)
    {
        std::ostringstream stream;
        stream << address;
        return stream.str();
    }

    /** @brief Returns the bytes of a buffer as a vector, so one comparison covers all of them. */
    std::vector<unsigned char> bytes_of(const unsigned char* data, std::size_t count)
    {
        return std::vector<unsigned char>(data, data + count);
    }

    /** @brief Builds one queue per SYCL device and an allocator over them. */
    class UsmAllocatorFixture : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Skips when SYCL reports no device; otherwise builds the allocator. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";

                for (const sycl::device& device : sycl::device::get_devices())
                    queues_.emplace_back(device);
                allocator_ = std::make_unique<USMAllocator>(
                    SushiRuntime::Internal::Memory::make_sycl_usm_source(queues_));
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

            /** @brief One queue per device; index i is the allocator's device i. */
            std::vector<sycl::queue> queues_;

            /** @brief The allocator under test, built over queues_. */
            std::unique_ptr<USMAllocator> allocator_;
    };

    /** @brief Fixture of the small tests: an allocator over a host source of two devices. */
    class HostAllocatorFixture : public ::testing::Test
    {
        protected:
            /** @brief Allocates K_BYTES of shared USM on the last device and checks it came. */
            [[nodiscard]] void* allocate_on_last_device()
            {
                void* const address = allocator_.allocate(
                    USMRequest{USMKind::Shared, K_BYTES, 0, K_HOST_DEVICES - 1});
                EXPECT_NE(address, nullptr);
                return address;
            }

            /** @brief The allocator under test, over a host source of K_HOST_DEVICES devices. */
            USMAllocator allocator_{make_host_usm_source(K_HOST_DEVICES)};
    };
} // namespace

/** @brief Fixture of the medium tests that read one allocator over every SYCL device. */
class Medium_UsmAllocator : public UsmAllocatorFixture
{
};

/** @brief Fixture of the medium tests that run once per kind. */
class Medium_UsmAllocatorKind : public UsmAllocatorFixture, public ::testing::WithParamInterface<USMKind>
{
};

/** @brief Fixture of the small tests that read one allocator over a host source. */
class Small_UsmAllocator : public HostAllocatorFixture
{
};

TEST_P(Medium_UsmAllocatorKind, AllocatesIsUsableIsCountedAndIsReleased)
{
    const USMKind kind = GetParam();
    const std::optional<std::size_t> device = device_supporting(kind);
    if (!device.has_value())
        GTEST_SKIP() << "No SYCL device supports this kind.";

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

INSTANTIATE_TEST_SUITE_P(Kinds, Medium_UsmAllocatorKind,
                         ::testing::Values(USMKind::Host, USMKind::Shared, USMKind::Device),
                         [](const ::testing::TestParamInfo<USMKind>& info) { return std::string(name_of(info.param)); });

TEST_F(Small_UsmAllocator, AnAllocationResolvesToItsDeviceAtItsBase)
{
    void* const address = allocate_on_last_device();

    EXPECT_EQ(allocator_.device_of(address), std::optional<std::size_t>(K_HOST_DEVICES - 1));
    EXPECT_EQ(allocator_.containing_base(address), std::optional<const void*>(address));
    allocator_.release(address);
}

TEST_F(Small_UsmAllocator, AnInteriorAddressFindsItsBase)
{
    void* const address = allocate_on_last_device();
    void* const interior = static_cast<unsigned char*>(address) + K_BYTES / 2;

    // device_of() answers for a first address only; containing_base() finds it from inside.
    EXPECT_FALSE(allocator_.device_of(interior).has_value());
    EXPECT_EQ(allocator_.containing_base(interior), std::optional<const void*>(address));
    EXPECT_EQ(allocator_.containing_base(address), std::optional<const void*>(address));
    allocator_.release(address);
}

TEST_F(Small_UsmAllocator, TheAddressPastTheEndHasNoBase)
{
    void* const address = allocate_on_last_device();

    EXPECT_FALSE(allocator_.containing_base(static_cast<unsigned char*>(address) + K_BYTES).has_value());
    allocator_.release(address);
}

TEST_F(Small_UsmAllocator, AReleasedAllocationHasNoBase)
{
    void* const address = allocate_on_last_device();
    void* const interior = static_cast<unsigned char*>(address) + K_BYTES / 2;

    allocator_.release(address);

    EXPECT_FALSE(allocator_.containing_base(interior).has_value());
}

TEST_F(Small_UsmAllocator, AForeignPointerHasNoDevice)
{
    int foreign = 0;

    EXPECT_FALSE(allocator_.device_of(&foreign).has_value());
    EXPECT_FALSE(allocator_.owning_device(&foreign).has_value());
    EXPECT_FALSE(allocator_.containing_base(&foreign).has_value());

    EXPECT_FALSE(allocator_.device_of(nullptr).has_value());
    EXPECT_FALSE(allocator_.owning_device(nullptr).has_value());
    EXPECT_FALSE(allocator_.containing_base(nullptr).has_value());
}

TEST_F(Small_UsmAllocator, AnAllocationIsOwnedByItsRecordedDevice)
{
    void* const address = allocate_on_last_device();

    EXPECT_EQ(allocator_.owning_device(address), std::optional<std::size_t>(K_HOST_DEVICES - 1));
    allocator_.release(address);
}

TEST_F(Medium_UsmAllocator, OwningDeviceAsksSYCLAboutUSMItHasNoRecordOf)
{
    const std::optional<std::size_t> device = device_supporting(USMKind::Shared);
    if (!device.has_value())
        GTEST_SKIP() << "No SYCL device supports shared USM.";

    // Allocated behind the allocator's back, in the context of one of its queues.
    void* const address = sycl::malloc_shared(K_BYTES, queues_[*device]);
    ASSERT_NE(address, nullptr);

    if (sycl::get_pointer_type(address, queues_[*device].get_context()) != sycl::usm::alloc::shared)
    {
        sycl::free(address, queues_[*device]);
        GTEST_SKIP() << "SYCL does not report this device's shared allocation as shared, so it names no owner.";
    }

    EXPECT_FALSE(allocator_->device_of(address).has_value());
    const std::optional<std::size_t> owning = allocator_->owning_device(address);
    EXPECT_EQ(owning, std::optional<std::size_t>(*device));

    sycl::free(address, queues_[*device]);
}

TEST_F(Small_UsmAllocator, RecordOfKeepsTheKindAnAllocationWasMadeAs)
{
    void* const address = allocate_on_last_device();
    const auto record = allocator_.record_of(address);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->device_index, K_HOST_DEVICES - 1);
    EXPECT_EQ(record->kind, std::optional<USMKind>(USMKind::Shared));
    allocator_.release(address);
    EXPECT_FALSE(allocator_.record_of(address).has_value());
}

TEST_F(Small_UsmAllocator, RecordOfAnAdoptedAddressHasNoKind)
{
    std::array<int, 8> external{};
    allocator_.adopt(external.data(), K_HOST_DEVICES - 1, sizeof(external), std::nullopt);

    const auto adopted = allocator_.record_of(external.data());

    ASSERT_TRUE(adopted.has_value());
    EXPECT_FALSE(adopted->kind.has_value());
    allocator_.forget(external.data());
}

TEST_F(Small_UsmAllocator, AdoptRecordsTheDeviceOfAnAddress)
{
    const std::size_t baseline = allocator_.live_count();
    std::array<int, 8> external{};

    allocator_.adopt(external.data(), K_HOST_DEVICES - 1, sizeof(external), std::nullopt);

    EXPECT_EQ(allocator_.live_count(), baseline + 1);
    EXPECT_EQ(allocator_.device_of(external.data()), std::optional<std::size_t>(K_HOST_DEVICES - 1));
    allocator_.forget(external.data());
}

TEST_F(Small_UsmAllocator, AnAdoptedAddressContainsItsBytes)
{
    std::array<int, 8> external{};
    allocator_.adopt(external.data(), K_HOST_DEVICES - 1, sizeof(external), std::nullopt);

    EXPECT_EQ(allocator_.containing_base(&external[3]), std::optional<const void*>(external.data()));
    allocator_.forget(external.data());
}

TEST_F(Small_UsmAllocator, ForgetDropsTheRecordWithoutFreeing)
{
    const std::size_t baseline = allocator_.live_count();
    std::array<int, 8> external{};
    allocator_.adopt(external.data(), K_HOST_DEVICES - 1, sizeof(external), std::nullopt);

    allocator_.forget(external.data());

    EXPECT_EQ(allocator_.live_count(), baseline);
    EXPECT_FALSE(allocator_.device_of(external.data()).has_value());
}

TEST_F(Small_UsmAllocator, ForgetIgnoresNull)
{
    const std::size_t baseline = allocator_.live_count();

    allocator_.forget(nullptr);

    EXPECT_EQ(allocator_.live_count(), baseline);
}

TEST_F(Small_UsmAllocator, ReleaseOfAnAdoptedAddressDropsTheRecordAndDoesNotFreeIt)
{
    const std::size_t baseline = allocator_.live_count();
    const auto external = std::make_unique<unsigned char[]>(K_BYTES);
    std::memset(external.get(), K_PATTERN, K_BYTES);

    allocator_.adopt(external.get(), 0, K_BYTES, std::nullopt);
    ASSERT_EQ(allocator_.live_count(), baseline + 1);

    // A free of this heap block through the source would free memory it never allocated.
    allocator_.release(external.get());

    EXPECT_EQ(allocator_.live_count(), baseline);
    EXPECT_FALSE(allocator_.device_of(external.get()).has_value());
    EXPECT_EQ(bytes_of(external.get(), K_BYTES), std::vector<unsigned char>(K_BYTES, K_PATTERN));
}

TEST_F(Small_UsmAllocator, TheFirstReleaseLogsNothing)
{
    void* const address = allocate_on_last_device();

    SushiRuntime::TestRunner::LogCapture capture;
    allocator_.release(address);

    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0u);
}

TEST_F(Small_UsmAllocator, ASecondReleaseLogsOneErrorWithTheAddressAndFreesNothing)
{
    void* const address = allocate_on_last_device();
    allocator_.release(address);
    const std::size_t baseline = allocator_.live_count();

    SushiRuntime::TestRunner::LogCapture capture;
    allocator_.release(address);

    EXPECT_EQ(allocator_.live_count(), baseline);
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Error, printed(address)));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Error, "no record"));
}

TEST_F(Small_UsmAllocator, ReleaseOfAnUnrecordedAddressLogsAndLeavesItAllocated)
{
    const auto unrecorded = std::make_unique<unsigned char[]>(K_BYTES);
    std::memset(unrecorded.get(), K_PATTERN, K_BYTES);
    const std::size_t baseline = allocator_.live_count();

    {
        SushiRuntime::TestRunner::LogCapture capture;
        allocator_.release(unrecorded.get());
        EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 1u);
    }

    EXPECT_EQ(allocator_.live_count(), baseline);
    EXPECT_EQ(bytes_of(unrecorded.get(), K_BYTES), std::vector<unsigned char>(K_BYTES, K_PATTERN));
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
    EXPECT_DEATH(allocator_->adopt(&external, bad_index, sizeof(external), std::nullopt), pattern);
}

TEST_F(Small_UsmAllocator, TheReleaseObserverHearsNothingBeforeARelease)
{
    RecordingUSMReleaseObserver observer;

    allocator_.set_release_observer(&observer);

    EXPECT_TRUE(observer.released().empty());
    allocator_.set_release_observer(nullptr);
}

TEST_F(Small_UsmAllocator, ANullReleaseAndAForgetAreNotHeard)
{
    RecordingUSMReleaseObserver observer;
    allocator_.set_release_observer(&observer);
    int external = 0;
    allocator_.adopt(&external, 0, sizeof(external), std::nullopt);

    allocator_.release(nullptr);
    allocator_.forget(&external);

    EXPECT_TRUE(observer.released().empty());
    allocator_.set_release_observer(nullptr);
}

TEST_F(Small_UsmAllocator, AnAdoptedAddressIsHeardWhenReleased)
{
    RecordingUSMReleaseObserver observer;
    allocator_.set_release_observer(&observer);
    int external = 0;
    allocator_.adopt(&external, 0, sizeof(external), std::nullopt);

    allocator_.release(&external);

    EXPECT_EQ(observer.released(), std::vector<void*>{&external});
    allocator_.set_release_observer(nullptr);
}

TEST_F(Small_UsmAllocator, ARemovedObserverHearsNothingMore)
{
    RecordingUSMReleaseObserver observer;
    allocator_.set_release_observer(&observer);
    void* const address = allocate_on_last_device();
    allocator_.set_release_observer(nullptr);

    allocator_.release(address);

    EXPECT_TRUE(observer.released().empty());
}

TEST_F(Medium_UsmAllocator, SetReleaseObserverReturnsOnlyAfterAnObserverCallInFlightFinished)
{
    BlockingObserver observer;
    allocator_->set_release_observer(&observer);
    int external = 0;
    allocator_->adopt(&external, 0, sizeof(external), std::nullopt);

    std::thread releaser([this, &external] { allocator_->release(&external); });
    observer.wait_until_entered();

    std::atomic<bool> removal_started{false};
    std::atomic<bool> call_finished_when_removal_returned{false};
    std::thread remover(
        [this, &observer, &removal_started, &call_finished_when_removal_returned]
        {
            removal_started.store(true);
            allocator_->set_release_observer(nullptr);
            call_finished_when_removal_returned.store(observer.has_finished());
        });

    // The call stays blocked until the remover has begun; a removal that skips the wait returns early.
    wait_until_set(removal_started);
    observer.open();
    remover.join();
    releaser.join();

    EXPECT_TRUE(call_finished_when_removal_returned.load());
}

TEST(Small_UsmAllocatorFailure, LogsTheRefusedRequest)
{
    USMAllocator allocator(make_host_usm_source());
    const std::size_t past_the_memory = std::numeric_limits<std::size_t>::max();

    SushiRuntime::TestRunner::LogCapture capture;
    void* const address =
        allocator.allocate(USMRequest{USMKind::Shared, past_the_memory, 64, 0});

    EXPECT_EQ(address, nullptr);
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, std::to_string(past_the_memory)));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "alignment 64"));
    EXPECT_EQ(capture.count_in("memory"), 1u);
}
