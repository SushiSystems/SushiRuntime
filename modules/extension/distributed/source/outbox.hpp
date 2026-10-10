/**************************************************************************/
/* outbox.hpp                                                             */
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
 * @file outbox.hpp
 * @brief Declares Outbox, one peer's send queue: control frames ahead of bulk streams.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names what Outbox::pop() returned.
         */
        enum class PopStatus : std::uint8_t
        {
            Frame,   /**< The frame was filled; write it. */
            Timeout, /**< Nothing became ready in the time given. */
            Closed,  /**< close() was called and no control frame is left. */
        };

        /**
         * @brief Queues what one peer is owed and hands it to that peer's single writer.
         *
         * Any thread pushes; one thread pops. The outbox owns a mutex and never holds it
         * while it calls FrameSource::next() or destroys a source, and it calls nothing else.
         * It knows no socket and no frame header, so both transports use it.
         */
        class SUSHIRUNTIME_API Outbox final
        {
            public:
                /**
                 * @brief Builds an open, empty outbox.
                 * @param control_capacity Most control frames queued at once; at least 1.
                 */
                explicit Outbox(std::size_t control_capacity);

                /** @brief Destroys the frames and the sources still queued. */
                ~Outbox();

                Outbox(const Outbox&) = delete;
                Outbox& operator=(const Outbox&) = delete;
                Outbox(Outbox&&) = delete;
                Outbox& operator=(Outbox&&) = delete;

                /**
                 * @brief Copies a control frame to the back of the control queue.
                 * @return Queued; QueueFull at the capacity; TooLong above 1 MiB of payload;
                 *         UnknownPeer once close() was called.
                 */
                [[nodiscard]] SendStatus push_control(MessageType type,
                                                      SushiRuntime::span<const std::byte> payload);

                /**
                 * @brief Adds a stream behind the streams already queued.
                 * @param source Not null; destroyed here, outside the lock, unless Queued.
                 * @return Queued, or UnknownPeer once close() was called.
                 */
                [[nodiscard]] SendStatus push_stream(std::unique_ptr<FrameSource> source);

                /**
                 * @brief Waits for the next frame and moves it into @p frame.
                 *
                 * The oldest control frame comes first. With none queued, the front stream is
                 * taken out, asked for one frame with the lock released, and put at the back
                 * unless the frame was its last; a Finished source is destroyed and skipped.
                 * @param timeout Longest wait; a negative value waits without a limit.
                 */
                [[nodiscard]] PopStatus pop(OutboundFrame& frame,
                                            std::chrono::milliseconds timeout);

                /**
                 * @brief Refuses every later push, drops the streams and wakes pop().
                 *
                 * Control frames already queued stay and pop() still returns them, so a REJECT
                 * or a GOODBYE queued just before the close is written.
                 */
                void close() noexcept;

                /** @brief Returns the number of control frames queued. */
                [[nodiscard]] std::size_t control_depth() const;

                /** @brief Returns the number of streams queued. */
                [[nodiscard]] std::size_t stream_count() const;

            private:
                /// @brief Guards every member below.
                mutable std::mutex mutex_;

                /// @brief Wakes pop() on a push and on close().
                std::condition_variable ready_;

                /// @brief Control frames in the order they were pushed.
                std::deque<OutboundFrame> control_;

                /// @brief Streams in the order they are served.
                std::deque<std::unique_ptr<FrameSource>> streams_;

                /// @brief Most control frames queued at once.
                std::size_t control_capacity_;

                /// @brief Set by close().
                bool closed_ = false;
        };

    } // namespace Distributed
} // namespace SushiRuntime
