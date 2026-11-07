/**************************************************************************/
/* unsent_frame_log.hpp                                                   */
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
 * @file unsent_frame_log.hpp
 * @brief Declares log_unsent_frame, the DEBUG line of a control frame the transport refused.
 * @author Mustafa Garip
 * @see modules/extension/cluster_monitor/README.md
 */

#pragma once

#include <SushiRuntime/cluster_monitor/cluster_monitor_log_category.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/transport/transport.hpp>

namespace SushiRuntime
{
    namespace Distributed::Detail
    {
        /** @brief Returns the words a log line uses for a SendStatus. */
        [[nodiscard]] inline const char* send_status_text(SendStatus status) noexcept
        {
            switch (status)
            {
                case SendStatus::Queued:
                    return "queued";
                case SendStatus::QueueFull:
                    return "the peer's control queue is full";
                case SendStatus::UnknownPeer:
                    return "the peer has no open connection";
                case SendStatus::TooLong:
                    return "the payload is too long";
                case SendStatus::Stopped:
                    return "the transport is stopped";
            }
            return "unknown";
        }

        /**
         * @brief Logs one DEBUG when a control frame was not queued; a queued frame logs nothing.
         * @param frame The frame's name, such as "PING".
         * @param peer The worker the frame was for.
         * @param status What the transport answered.
         */
        inline void log_unsent_frame(const char* frame, PeerID peer, SendStatus status)
        {
            if (status == SendStatus::Queued)
                return;
            SR_LOG_DEBUG(K_CLUSTER_MONITOR_LOG_CATEGORY,
                         "{} to worker {} was not queued: {}; the monitor goes on without it",
                         frame, peer.value, send_status_text(status));
        }
    } // namespace Distributed::Detail
} // namespace SushiRuntime
