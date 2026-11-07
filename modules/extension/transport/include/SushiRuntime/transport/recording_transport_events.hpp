/**************************************************************************/
/* recording_transport_events.hpp                                         */
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
 * @file recording_transport_events.hpp
 * @brief Declares RecordingTransportEvents, a TransportEvents that records its calls.
 * @author Mustafa Garip
 * @see modules/extension/transport/README.md
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds one frame a RecordingTransportEvents received, with a copy of its payload.
         */
        struct RecordedFrame
        {
            /** @brief Holds the connection the frame arrived on. */
            PeerID peer;

            /** @brief Holds the frame's type. */
            MessageType type = MessageType::Hello;

            /** @brief Holds the frame's flag byte. */
            std::uint8_t flags = 0;

            /** @brief Holds a copy of the payload, which the transport lends for the call only. */
            std::vector<std::byte> payload;
        };

        /**
         * @brief Holds one connection loss a RecordingTransportEvents heard.
         */
        struct RecordedLoss
        {
            /** @brief Holds the connection that ended. */
            PeerID peer;

            /** @brief Holds the cause and text the transport gave. */
            PeerLoss loss;
        };

        /**
         * @brief Records every event a transport makes, in the order it made them.
         *
         * The record grows on every call and each wait returns as soon as its count is reached,
         * so a test needs no sleep. This adapter is the test double of the port, not a session
         * layer: it answers nothing and sends nothing.
         * @see modules/extension/transport/README.md
         */
        class RecordingTransportEvents final : public TransportEvents
        {
            public:
                /** @brief Builds a receiver that heard nothing. */
                RecordingTransportEvents() = default;

                /** @brief Destroys the receiver through its own type. */
                ~RecordingTransportEvents() = default;

                RecordingTransportEvents(const RecordingTransportEvents&) = delete;
                RecordingTransportEvents& operator=(const RecordingTransportEvents&) = delete;
                RecordingTransportEvents(RecordingTransportEvents&&) = delete;
                RecordingTransportEvents& operator=(RecordingTransportEvents&&) = delete;

                /** @brief Records the connection that opened. */
                void on_peer_connected(PeerID peer, const PeerDescription& /*info*/) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        connected_.push_back(peer);
                    }
                    changed_.notify_all();
                }

                /** @brief Records the frame with a copy of its payload. */
                void on_frame(PeerID peer, MessageType type, std::uint8_t flags,
                              SushiRuntime::span<const std::byte> payload) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        frames_.push_back(RecordedFrame{
                            peer, type, flags,
                            std::vector<std::byte>(payload.begin(), payload.end())});
                    }
                    changed_.notify_all();
                }

                /** @brief Records the connection that ended and why. */
                void on_peer_lost(PeerID peer, const PeerLoss& loss) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        losses_.push_back(RecordedLoss{peer, loss});
                    }
                    changed_.notify_all();
                }

                /** @brief Records the cause the transport gave up connecting with. */
                void on_connect_abandoned(const std::string& cause) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        abandoned_.push_back(cause);
                    }
                    changed_.notify_all();
                }

                /** @brief Returns the connections that opened, in the order they opened. */
                [[nodiscard]] std::vector<PeerID> connected() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return connected_;
                }

                /** @brief Returns every frame received, in arrival order. */
                [[nodiscard]] std::vector<RecordedFrame> frames() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return frames_;
                }

                /** @brief Returns the frames of one type, in arrival order. */
                [[nodiscard]] std::vector<RecordedFrame> frames_of(MessageType type) const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    std::vector<RecordedFrame> selected;
                    std::copy_if(frames_.begin(), frames_.end(), std::back_inserter(selected),
                                 [type](const RecordedFrame& frame)
                                 {
                                     return frame.type == type;
                                 });
                    return selected;
                }

                /** @brief Returns how many frames of one type arrived. */
                [[nodiscard]] std::size_t frame_count(MessageType type) const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return count_locked(type);
                }

                /** @brief Returns the connection losses, in the order they were heard. */
                [[nodiscard]] std::vector<RecordedLoss> losses() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return losses_;
                }

                /** @brief Returns the causes of every abandoned connect, in the order heard. */
                [[nodiscard]] std::vector<std::string> abandoned() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return abandoned_;
                }

                /**
                 * @brief Waits until @p count connections have opened.
                 * @return False when @p limit passed first.
                 */
                [[nodiscard]] bool wait_for_connections(std::size_t count,
                                                        std::chrono::milliseconds limit) const
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, limit, [this, count]
                                             {
                                                 return connected_.size() >= count;
                                             });
                }

                /**
                 * @brief Waits until @p count frames of @p type have arrived.
                 * @return False when @p limit passed first.
                 */
                [[nodiscard]] bool wait_for_frames(MessageType type, std::size_t count,
                                                   std::chrono::milliseconds limit) const
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, limit, [this, type, count]
                                             {
                                                 return count_locked(type) >= count;
                                             });
                }

                /**
                 * @brief Waits until @p count connections have ended.
                 * @return False when @p limit passed first.
                 */
                [[nodiscard]] bool wait_for_losses(std::size_t count,
                                                   std::chrono::milliseconds limit) const
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, limit, [this, count]
                                             {
                                                 return losses_.size() >= count;
                                             });
                }

            private:
                /** @brief Returns the number of frames of a type; the mutex is held. */
                [[nodiscard]] std::size_t count_locked(MessageType type) const
                {
                    return static_cast<std::size_t>(std::count_if(
                        frames_.begin(), frames_.end(),
                        [type](const RecordedFrame& frame)
                        {
                            return frame.type == type;
                        }));
                }

                /// @brief Guards every member below.
                mutable std::mutex mutex_;

                /// @brief Wakes the waits on every recorded event.
                mutable std::condition_variable changed_;

                /// @brief The connections that opened.
                std::vector<PeerID> connected_;

                /// @brief The frames received.
                std::vector<RecordedFrame> frames_;

                /// @brief The connections that ended.
                std::vector<RecordedLoss> losses_;

                /// @brief The causes of the abandoned connects.
                std::vector<std::string> abandoned_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
