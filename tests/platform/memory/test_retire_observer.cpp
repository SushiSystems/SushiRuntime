/**************************************************************************/
/* test_retire_observer.cpp                                               */
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
 * @file test_retire_observer.cpp
 * @brief Tests RetireObserver: the directory tells an observer of each handle it retires.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/memory/retire_observer.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    /** @brief Universe the directories of these tests mint in. */
    constexpr std::uint32_t K_UNIVERSE = 3;

    /** @brief Keeps each handle it hears, in arrival order. */
    class RecordingObserver final : public RetireObserver
    {
        public:
            void on_retire(GlobalHandle handle) noexcept override
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                heard_.push_back(handle);
            }

            /** @brief Returns the handles heard so far, oldest first. */
            [[nodiscard]] std::vector<GlobalHandle> heard() const
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return heard_;
            }

        private:
            /// @brief Guards heard_.
            mutable std::mutex mutex_;
            /// @brief The handles heard.
            std::vector<GlobalHandle> heard_;
    };

    /** @brief Blocks every on_retire() call until the test opens it. */
    class BlockingObserver final : public RetireObserver
    {
        public:
            void on_retire(GlobalHandle) noexcept override
            {
                std::unique_lock<std::mutex> lock(mutex_);
                entered_ = true;
                changed_.notify_all();
                changed_.wait(lock, [this] { return opened_; });
                finished_ = true;
            }

            /** @brief Blocks until one on_retire() call is waiting. */
            void wait_until_entered()
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [this] { return entered_; });
            }

            /** @brief Lets every waiting on_retire() call return. */
            void open()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                opened_ = true;
                changed_.notify_all();
            }

            /** @brief Returns whether an on_retire() call ran to its end. */
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
            /// @brief Whether an on_retire() call has begun.
            bool entered_ = false;
            /// @brief Whether waiting calls may return.
            bool opened_ = false;
            /// @brief Whether an on_retire() call has run to its end.
            bool finished_ = false;
    };

    /** @brief Returns a handle minted in directory. */
    GlobalHandle mint_buffer(ResidencyDirectory& directory)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        return outcome.handle;
    }
} // namespace

TEST(Small_RetireObserver, HearsEachRetiredHandleOnce)
{
    ResidencyDirectory directory(K_UNIVERSE);
    RecordingObserver observer;
    directory.set_retire_observer(&observer);
    const GlobalHandle handle = mint_buffer(directory);

    ASSERT_EQ(directory.retire(handle), HandleStatus::Ok);
    EXPECT_EQ(directory.retire(handle), HandleStatus::StaleHandle);

    EXPECT_EQ(observer.heard(), std::vector<GlobalHandle>{handle});
    directory.set_retire_observer(nullptr);
}

TEST(Small_RetireObserver, ARemovedObserverHearsNothingMore)
{
    ResidencyDirectory directory(K_UNIVERSE);
    RecordingObserver observer;
    directory.set_retire_observer(&observer);
    const GlobalHandle handle = mint_buffer(directory);
    directory.set_retire_observer(nullptr);

    ASSERT_EQ(directory.retire(handle), HandleStatus::Ok);

    EXPECT_TRUE(observer.heard().empty());
}

TEST(Small_RetireObserver, AnObserverMayMintFromInsideTheCall)
{
    ResidencyDirectory directory(K_UNIVERSE);
    /** @brief Mints one handle from inside on_retire(), which needs the writer lock free. */
    class MintingObserver final : public RetireObserver
    {
        public:
            explicit MintingObserver(ResidencyDirectory& target) : directory(target) {}
            void on_retire(GlobalHandle) noexcept override
            {
                minted = directory.mint(HandleKind::Buffer, HandleFlags::None).status;
            }
            ResidencyDirectory& directory;
            HandleStatus minted = HandleStatus::NullHandle;
    } observer(directory);
    directory.set_retire_observer(&observer);

    ASSERT_EQ(directory.retire(mint_buffer(directory)), HandleStatus::Ok);

    EXPECT_EQ(observer.minted, HandleStatus::Ok);
    directory.set_retire_observer(nullptr);
}

TEST(Medium_RetireObserver, SetRetireObserverReturnsOnlyAfterACallInFlightFinished)
{
    ResidencyDirectory directory(K_UNIVERSE);
    BlockingObserver observer;
    directory.set_retire_observer(&observer);
    const GlobalHandle handle = mint_buffer(directory);

    std::thread retirer([&directory, handle] { static_cast<void>(directory.retire(handle)); });
    observer.wait_until_entered();

    std::atomic<bool> removal_started{false};
    std::atomic<bool> finished_when_removal_returned{false};
    std::thread remover([&directory, &observer, &removal_started, &finished_when_removal_returned]
    {
        removal_started.store(true);
        directory.set_retire_observer(nullptr);
        finished_when_removal_returned.store(observer.has_finished());
    });

    while (!removal_started.load())
        std::this_thread::yield();
    observer.open();
    remover.join();
    retirer.join();

    EXPECT_TRUE(finished_when_removal_returned.load());
}
