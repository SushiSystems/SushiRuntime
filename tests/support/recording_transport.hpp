/**************************************************************************/
/* recording_transport.hpp                                                */
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
 * @file recording_transport.hpp
 * @brief Declares RecordingTransport, a Transport that keeps every frame it is given and sends none.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace TestSupport
{

/**
 * @brief Holds one frame a unit handed to the transport.
 */
struct SentFrame
{
    SushiRuntime::Distributed::PeerID peer;               /**< The peer the frame was for. */
    SushiRuntime::Distributed::MessageType type =
        SushiRuntime::Distributed::MessageType::Hello;    /**< The frame's type. */
    std::vector<std::byte> payload;                       /**< The payload as sent. */
};

/**
 * @brief Implements Transport with no thread and no connection: send() records and answers Queued.
 *
 * A unit that sends frames and closes peers is tested against it: the test reads sent() and
 * closed() afterwards. start() keeps the events receiver, which the test reads through events()
 * to feed the unit a frame or a loss.
 */
class RecordingTransport final : public SushiRuntime::Distributed::Transport
{
    public:
        /** @brief Keeps the events receiver and answers Ok. */
        [[nodiscard]] SushiRuntime::Distributed::TransportResult start(
            SushiRuntime::Distributed::TransportEvents& events) override
        {
            events_ = &events;
            return {};
        }

        /** @brief Counts the call. */
        void stop() noexcept override
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            ++stops_;
        }

        /** @brief Records the frame and answers Queued, or the status set by refuse_with(). */
        [[nodiscard]] SushiRuntime::Distributed::SendStatus send(
            SushiRuntime::Distributed::PeerID peer, SushiRuntime::Distributed::MessageType type,
            SushiRuntime::span<const std::byte> payload) override
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (refusal_ != SushiRuntime::Distributed::SendStatus::Queued)
                return refusal_;
            sent_.push_back(SentFrame{peer, type, std::vector<std::byte>(payload.begin(), payload.end())});
            return SushiRuntime::Distributed::SendStatus::Queued;
        }

        /** @brief Refuses a stream: the recorder carries control frames only. */
        [[nodiscard]] SushiRuntime::Distributed::SendStatus send_stream(
            SushiRuntime::Distributed::PeerID /*peer*/,
            std::unique_ptr<SushiRuntime::Distributed::FrameSource> /*source*/) override
        {
            return SushiRuntime::Distributed::SendStatus::UnknownPeer;
        }

        /** @brief Records the peer. */
        void close_peer(SushiRuntime::Distributed::PeerID peer) noexcept override
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_.push_back(peer);
        }

        /** @brief Answers Loopback. */
        [[nodiscard]] SushiRuntime::Distributed::TransportExposure exposure() const noexcept override
        {
            return SushiRuntime::Distributed::TransportExposure::Loopback;
        }

        /** @brief Answers zero counters. */
        [[nodiscard]] SushiRuntime::Distributed::TransportStatistics statistics() const noexcept override
        {
            return {};
        }

        /** @brief Makes every later send() answer @p status without recording the frame. */
        void refuse_with(SushiRuntime::Distributed::SendStatus status)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            refusal_ = status;
        }

        /** @brief Returns the frames sent so far, in order. */
        [[nodiscard]] std::vector<SentFrame> sent() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return sent_;
        }

        /** @brief Returns the peers closed so far, in order. */
        [[nodiscard]] std::vector<SushiRuntime::Distributed::PeerID> closed() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return closed_;
        }

        /** @brief Returns how often stop() was called. */
        [[nodiscard]] std::size_t stops() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return stops_;
        }

        /** @brief Returns the receiver start() was given, or null before it. */
        [[nodiscard]] SushiRuntime::Distributed::TransportEvents* events() const noexcept
        {
            return events_;
        }

    private:
        mutable std::mutex mutex_;                                      /**< Guards the records. */
        std::vector<SentFrame> sent_;                                   /**< The frames sent. */
        std::vector<SushiRuntime::Distributed::PeerID> closed_;         /**< The peers closed. */
        std::size_t stops_ = 0;                                         /**< The calls to stop(). */
        SushiRuntime::Distributed::SendStatus refusal_ =
            SushiRuntime::Distributed::SendStatus::Queued;              /**< The answer to send(). */
        SushiRuntime::Distributed::TransportEvents* events_ = nullptr;  /**< The receiver of start(). */
};

} // namespace TestSupport
