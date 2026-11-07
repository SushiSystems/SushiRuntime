/**************************************************************************/
/* transport_drives.cpp                                                   */
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
 * @file transport_drives.cpp
 * @brief Defines ThreadDrive and ExecutorDrive, the two TransportDrive adapters.
 * @author Mustafa Garip
 * @see modules/extension/in_process_transport/README.md
 */

#include <SushiRuntime/in_process_transport/executor_drive.hpp>
#include <SushiRuntime/in_process_transport/thread_drive.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        ThreadDrive::~ThreadDrive()
        {
            stop();
        }

        Result<void> ThreadDrive::start(Round round)
        {
            round_ = std::move(round);
            try
            {
                thread_ = std::thread([this]() { run(); });
            }
            catch (const std::system_error& error)
            {
                return make_error(Error{ErrorCode::AllocationFailed,
                                        std::string("cannot start the in-process pump thread: ") +
                                            error.what()});
            }
            return {};
        }

        void ThreadDrive::post_round()
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                requested_ = true;
            }
            wake_.notify_one();
        }

        void ThreadDrive::post_round_at(std::uint64_t at_ns)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (!wake_at_ || at_ns < *wake_at_)
                    wake_at_ = at_ns;
            }
            wake_.notify_one();
        }

        void ThreadDrive::stop() noexcept
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            wake_.notify_all();
            if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
                thread_.join();
        }

        std::uint64_t ThreadDrive::now() const noexcept
        {
            return clock_.now_ns();
        }

        void ThreadDrive::run()
        {
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    const auto is_due = [this]()
                    {
                        return stopping_ || requested_ ||
                               (wake_at_ && clock_.now_ns() >= *wake_at_);
                    };
                    // The steady clock is the one now() reads, so a deadline is steady time.
                    if (wake_at_)
                        wake_.wait_until(lock,
                                         std::chrono::steady_clock::time_point(
                                             std::chrono::nanoseconds(*wake_at_)),
                                         is_due);
                    else
                        wake_.wait(lock, is_due);
                    if (stopping_)
                        return;
                    if (!requested_ && !(wake_at_ && clock_.now_ns() >= *wake_at_))
                        continue;
                    requested_ = false;
                    if (wake_at_ && clock_.now_ns() >= *wake_at_)
                        wake_at_.reset();
                }
                round_();
            }
        }

        /** @brief The round of an ExecutorDrive and whether it still runs, shared with its jobs. */
        struct ExecutorDrive::Shared
        {
            std::mutex mutex;      /**< Guards both fields. */
            Round round;           /**< The round start() bound. */
            bool running = false;  /**< Set by start(), cleared by stop(). */
        };

        ExecutorDrive::ExecutorDrive(std::shared_ptr<Executor> executor,
                                     std::shared_ptr<const Clock> clock)
            : executor_(std::move(executor)),
              clock_(std::move(clock)),
              shared_(std::make_shared<Shared>())
        {
            SR_VERIFY(executor_ != nullptr, "ExecutorDrive: pass an executor, not null.");
            SR_VERIFY(clock_ != nullptr, "ExecutorDrive: pass a clock, not null.");
        }

        ExecutorDrive::~ExecutorDrive()
        {
            stop();
        }

        Result<void> ExecutorDrive::start(Round round)
        {
            const std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->round = std::move(round);
            shared_->running = true;
            return {};
        }

        void ExecutorDrive::post_round()
        {
            const std::shared_ptr<Shared> shared = shared_;
            executor_->post([shared]()
            {
                Round round;
                {
                    const std::lock_guard<std::mutex> lock(shared->mutex);
                    if (!shared->running)
                        return;
                    round = shared->round;
                }
                round();
            });
        }

        void ExecutorDrive::post_round_at(std::uint64_t /*at_ns*/)
        {

        }

        void ExecutorDrive::stop() noexcept
        {
            const std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->running = false;
        }

        std::uint64_t ExecutorDrive::now() const noexcept
        {
            return clock_->now_ns();
        }
    } // namespace Distributed
} // namespace SushiRuntime
