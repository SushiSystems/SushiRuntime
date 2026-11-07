/**************************************************************************/
/* executor_fabric.hpp                                                    */
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
 * @file executor_fabric.hpp
 * @brief Declares ExecutorFabric, a master and a worker endpoint of one in-process fabric that a test steps.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/in_process_transport/executor_drive.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

#include "support/single_frame.hpp"

namespace TestSupport
{
    /**
     * @brief Joins a master and a worker endpoint of an InProcessFabric, both on ExecutorDrives
     *        of one SeededExecutor and one ManualClock, so a test steps every frame.
     *
     * The unit under test takes one endpoint; the test reads what it sent at the other end, in
     * the events that end records, and sends to it from there. Nothing runs between two calls:
     * settle() runs every posted round, and advance() moves the clock and wakes the endpoints.
     */
    class ExecutorFabric final
    {
        public:
            /** @brief Seed of the executor, so a failing order replays. */
            static constexpr std::uint64_t K_SEED = 0x5EED;

            /** @brief Builds the two endpoints; neither is started. */
            ExecutorFabric()
                : master_(SushiRuntime::Distributed::InProcessTransport::make_master(
                      fabric_, SushiRuntime::Distributed::InProcessMasterOptions(), make_drive())),
                  worker_(SushiRuntime::Distributed::InProcessTransport::make_worker(
                      fabric_, SushiRuntime::Distributed::InProcessWorkerOptions(), make_drive()))
            {

            }

            /** @brief Stops both endpoints before the events they report to go. */
            ~ExecutorFabric()
            {
                worker_->stop();
                master_->stop();
            }

            ExecutorFabric(const ExecutorFabric&) = delete;
            ExecutorFabric& operator=(const ExecutorFabric&) = delete;
            ExecutorFabric(ExecutorFabric&&) = delete;
            ExecutorFabric& operator=(ExecutorFabric&&) = delete;

            /** @brief Starts the master endpoint, which records at its end, and settles. */
            void start_master()
            {
                ASSERT_TRUE(master_->start(master_events_).is_ok());
                settle();
            }

            /** @brief Starts the worker endpoint on the events of a unit, and settles. */
            void start_worker(SushiRuntime::Distributed::TransportEvents& events)
            {
                ASSERT_TRUE(worker_->start(events).is_ok());
                settle();
            }

            /** @brief Starts the worker endpoint on the events it records itself, and settles. */
            void start_worker_recording()
            {
                start_worker(worker_events_);
            }

            /** @brief Runs every round posted so far, and the rounds those post. */
            void settle()
            {
                executor_->run_all();
            }

            /** @brief Moves the clock by @p nanoseconds, wakes the endpoints and settles. */
            void advance(std::uint64_t nanoseconds)
            {
                clock_->advance(nanoseconds);
                fabric_.wake_all();
                settle();
            }

            /**
             * @brief Encodes a message, sends it from the master to the worker over the open
             *        connection and settles.
             */
            template <typename Message>
            void deliver(const Message& message, std::uint8_t flags = 0)
            {
                std::vector<std::byte> payload;
                ASSERT_EQ(message.encode(payload), SushiRuntime::Distributed::Wire::CodecStatus::Ok);
                if (flags == 0)
                {
                    EXPECT_EQ(master_->send(master_peer(), Message::K_TYPE,
                                            SushiRuntime::span<const std::byte>(payload.data(),
                                                                                payload.size())),
                              SushiRuntime::Distributed::SendStatus::Queued);
                }
                else
                {
                    deliver_flagged(Message::K_TYPE, flags, std::move(payload));
                }
                settle();
            }

            /** @brief Closes the connection from the master's end and settles. */
            void lose()
            {
                master_->close_peer(master_peer());
                settle();
            }

            /** @brief Returns the worker endpoint, which the unit under test runs on. */
            [[nodiscard]] std::shared_ptr<SushiRuntime::Distributed::InProcessTransport>
            worker() const
            {
                return worker_;
            }

            /** @brief Returns the master endpoint. */
            [[nodiscard]] std::shared_ptr<SushiRuntime::Distributed::InProcessTransport>
            master() const
            {
                return master_;
            }

            /** @brief Returns what the master endpoint received from the worker. */
            [[nodiscard]] SushiRuntime::Distributed::RecordingTransportEvents& master_events()
            {
                return master_events_;
            }

            /** @brief Returns what the worker endpoint received, when start_worker_recording(). */
            [[nodiscard]] SushiRuntime::Distributed::RecordingTransportEvents& worker_events()
            {
                return worker_events_;
            }

            /** @brief Returns the name the master gives its newest connection to the worker. */
            [[nodiscard]] SushiRuntime::Distributed::PeerID master_peer() const
            {
                const std::vector<SushiRuntime::Distributed::PeerID> peers =
                    master_events_.connected();
                return peers.empty() ? SushiRuntime::Distributed::PeerID{} : peers.back();
            }

            /** @brief Returns the clock the endpoints read; the test advances it. */
            [[nodiscard]] SushiRuntime::ManualClock& clock() noexcept
            {
                return *clock_;
            }

            /** @brief Returns the clock as a shared pointer, for a unit that holds one. */
            [[nodiscard]] std::shared_ptr<SushiRuntime::ManualClock> shared_clock() const
            {
                return clock_;
            }

            /** @brief Returns the executor the rounds run on. */
            [[nodiscard]] std::shared_ptr<SushiRuntime::SeededExecutor> executor() const
            {
                return executor_;
            }

        private:
            /** @brief Returns a drive over the fabric's executor and clock. */
            [[nodiscard]] std::unique_ptr<SushiRuntime::Distributed::TransportDrive> make_drive()
                const
            {
                return std::make_unique<SushiRuntime::Distributed::ExecutorDrive>(executor_,
                                                                                    clock_);
            }

            /** @brief Sends a payload as a single-frame stream that carries @p flags. */
            void deliver_flagged(SushiRuntime::Distributed::MessageType type, std::uint8_t flags,
                                 std::vector<std::byte> payload)
            {
                SushiRuntime::Distributed::OutboundFrame frame;
                frame.type = type;
                frame.flags = flags;
                frame.payload = std::move(payload);
                EXPECT_EQ(master_->send_stream(master_peer(), std::make_unique<SingleFrame>(std::move(frame))),
                          SushiRuntime::Distributed::SendStatus::Queued);
            }

            /// @brief Runs the pump rounds of both endpoints.
            std::shared_ptr<SushiRuntime::SeededExecutor> executor_ =
                std::make_shared<SushiRuntime::SeededExecutor>(K_SEED);

            /// @brief Times both endpoints; it moves only when the test advances it.
            std::shared_ptr<SushiRuntime::ManualClock> clock_ =
                std::make_shared<SushiRuntime::ManualClock>();

            /// @brief The fabric both endpoints attach to.
            SushiRuntime::Distributed::InProcessFabric fabric_;

            /// @brief What the master endpoint receives; it outlives the endpoint.
            SushiRuntime::Distributed::RecordingTransportEvents master_events_;

            /// @brief What the worker endpoint receives when no unit takes its events.
            SushiRuntime::Distributed::RecordingTransportEvents worker_events_;

            /// @brief The master endpoint.
            std::shared_ptr<SushiRuntime::Distributed::InProcessTransport> master_;

            /// @brief The worker endpoint.
            std::shared_ptr<SushiRuntime::Distributed::InProcessTransport> worker_;
    };
} // namespace TestSupport
