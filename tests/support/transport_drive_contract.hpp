/**************************************************************************/
/* transport_drive_contract.hpp                                           */
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
 * @file transport_drive_contract.hpp
 * @brief Declares TransportDriveContract, the typed suite every TransportDrive adapter passes.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include <gtest/gtest.h>

#include <SushiRuntime/in_process_transport/transport_drive.hpp>

namespace TestSupport
{
    /** @brief Counts the rounds a drive ran and lets a test wait for a count. */
    class RoundCounter final
    {
        public:
            /** @brief Counts one round and wakes a waiter. */
            void count()
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    ++rounds_;
                }
                counted_.notify_all();
            }

            /** @brief Blocks until at least @p rounds rounds ran. */
            void wait_for(int rounds)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                counted_.wait(lock, [this, rounds]() { return rounds_ >= rounds; });
            }

            /** @brief Returns the rounds counted so far. */
            [[nodiscard]] int rounds()
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                return rounds_;
            }

        private:
            /** @brief Guards rounds_. */
            std::mutex mutex_;

            /** @brief Wakes wait_for(). */
            std::condition_variable counted_;

            /** @brief The rounds counted. */
            int rounds_ = 0;
    };

    /**
     * @brief Fixture of the contract every TransportDrive adapter passes.
     * @tparam Maker A default-constructible type with make(), returning the drive under test, and
     *         settle(RoundCounter&, int), returning once the rounds asked for so far have run.
     */
    template <class Maker>
    class TransportDriveContract : public ::testing::Test
    {
        protected:
            /** @brief Stops the drive before the counter it counts into goes away. */
            void TearDown() override { drive_->stop(); }

            /** @brief Binds a round that counts into counter_ and starts the drive. */
            void start_counting()
            {
                TestSupport::RoundCounter* const counter = &counter_;
                ASSERT_TRUE(drive_->start([counter]() { counter->count(); }).has_value());
            }

            /** @brief Builds the drive and runs what it needs to settle. */
            Maker maker_;

            /** @brief Counts the rounds the drive ran. */
            RoundCounter counter_;

            /** @brief The drive under test, one per case. */
            std::unique_ptr<SushiRuntime::Distributed::TransportDrive> drive_ = maker_.make();
    };

    TYPED_TEST_SUITE_P(TransportDriveContract);

    /** @brief Tests that a posted round runs. */
    TYPED_TEST_P(TransportDriveContract, APostedRoundRuns)
    {
        this->start_counting();

        this->drive_->post_round();
        this->maker_.settle(this->counter_, 1);

        EXPECT_EQ(this->counter_.rounds(), 1);
    }

    /** @brief Tests that a round posted after stop() does not run. */
    TYPED_TEST_P(TransportDriveContract, NoRoundRunsAfterStop)
    {
        this->start_counting();
        this->drive_->stop();

        this->drive_->post_round();
        this->maker_.settle(this->counter_, 0);

        EXPECT_EQ(this->counter_.rounds(), 0);
    }

    /** @brief Tests that a later reading of the drive's time is never smaller. */
    TYPED_TEST_P(TransportDriveContract, ALaterReadingIsNeverSmaller)
    {
        const std::uint64_t first = this->drive_->now();

        EXPECT_GE(this->drive_->now(), first);
    }

    REGISTER_TYPED_TEST_SUITE_P(TransportDriveContract, APostedRoundRuns, NoRoundRunsAfterStop,
                                ALaterReadingIsNeverSmaller);
} // namespace TestSupport
