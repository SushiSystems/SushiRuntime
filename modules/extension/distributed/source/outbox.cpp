/**************************************************************************/
/* outbox.cpp                                                             */
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
 * @file outbox.cpp
 * @brief Queues one peer's control frames and streams and hands them to its writer.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "outbox.hpp"

#include <algorithm>
#include <utility>

#include "wire_frame.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        Outbox::Outbox(std::size_t control_capacity)
            : control_capacity_(std::max<std::size_t>(control_capacity, 1))
        {

        }

        Outbox::~Outbox() = default;

        SendStatus Outbox::push_control(MessageType type,
                                        SushiRuntime::span<const std::byte> payload)
        {
            if (payload.size() > Wire::K_MAX_FRAME_PAYLOAD)
                return SendStatus::TooLong;

            // The copy is made before the lock, so the lock covers one deque insertion.
            OutboundFrame frame;
            frame.type = type;
            frame.payload.assign(payload.data(), payload.data() + payload.size());

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closed_)
                    return SendStatus::UnknownPeer;
                if (control_.size() >= control_capacity_)
                    return SendStatus::QueueFull;
                control_.push_back(std::move(frame));
            }
            ready_.notify_one();
            return SendStatus::Queued;
        }

        SendStatus Outbox::push_stream(std::unique_ptr<FrameSource> source)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!closed_)
                {
                    streams_.push_back(std::move(source));
                    source = nullptr;
                }
            }
            if (source != nullptr)
            {
                source.reset();
                return SendStatus::UnknownPeer;
            }
            ready_.notify_one();
            return SendStatus::Queued;
        }

        PopStatus Outbox::pop(OutboundFrame& frame, std::chrono::milliseconds timeout)
        {
            const bool is_bounded = timeout.count() >= 0;
            const std::chrono::steady_clock::time_point deadline =
                std::chrono::steady_clock::now() +
                (is_bounded ? timeout : std::chrono::milliseconds::zero());

            std::unique_lock<std::mutex> lock(mutex_);
            for (;;)
            {
                if (!control_.empty())
                {
                    std::swap(frame, control_.front());
                    control_.pop_front();
                    return PopStatus::Frame;
                }

                if (!streams_.empty())
                {
                    std::unique_ptr<FrameSource> source = std::move(streams_.front());
                    streams_.pop_front();
                    lock.unlock();

                    frame.flags = 0;
                    const FrameSourceStatus produced = source->next(frame);

                    lock.lock();
                    const bool is_kept = produced == FrameSourceStatus::Frame && !closed_;
                    const bool is_dropped = closed_;
                    if (is_kept)
                        streams_.push_back(std::move(source));
                    lock.unlock();
                    source.reset();

                    if (produced != FrameSourceStatus::Finished && !is_dropped)
                        return PopStatus::Frame;
                    lock.lock();
                    continue;
                }

                if (closed_)
                    return PopStatus::Closed;

                if (!is_bounded)
                    ready_.wait(lock);
                else if (ready_.wait_until(lock, deadline) == std::cv_status::timeout &&
                         control_.empty() && streams_.empty() && !closed_)
                    return PopStatus::Timeout;
            }
        }

        void Outbox::close() noexcept
        {
            for (;;)
            {
                std::unique_ptr<FrameSource> dropped;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    closed_ = true;
                    if (streams_.empty())
                        break;
                    dropped = std::move(streams_.front());
                    streams_.pop_front();
                }
                dropped.reset();
            }
            ready_.notify_all();
        }

        std::size_t Outbox::control_depth() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return control_.size();
        }

        std::size_t Outbox::stream_count() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return streams_.size();
        }

    } // namespace Distributed
} // namespace SushiRuntime
