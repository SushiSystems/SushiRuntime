/**************************************************************************/
/* link_tick.hpp                                                          */
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
 * @file link_tick.hpp
 * @brief Declares the pure step of the link's thread: what is due at a given time.
 * @author Mustafa Garip
 * @see modules/extension/master_link/README.md
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names how far the handshake of the watched connection has come.
         */
        enum class LinkPhase : std::uint8_t
        {
            Idle,       /**< No connection. */
            HelloSent,  /**< HELLO went out; CHALLENGE is awaited. */
            ProofSent,  /**< PROOF went out; WELCOME is awaited. */
            Registered, /**< WELCOME arrived. */
            Closing,    /**< The link closed a silent master and waits for the loss. */
        };

        /**
         * @brief Names what the link's thread does once the mutex is released.
         */
        enum class LinkAction : std::uint8_t
        {
            Wait,          /**< Nothing is due. */
            StopTransport, /**< Stop the transport of a link that reached a final end. */
            Heartbeat,     /**< Send HEARTBEAT. */
            Restart,       /**< Send HELLO again with a fresh nonce. */
            CloseSilent,   /**< Close a master that sent nothing for the liveness timeout. */
        };

        /**
         * @brief Holds the state of the link that decides what is due.
         */
        struct LinkTickInput
        {
            /** @brief Tells whether the link serves: not ended and not leaving. */
            bool running = false;

            /** @brief Tells whether the link reached a final end without leaving. */
            bool ended = false;

            /** @brief Tells whether the thread already stopped the transport. */
            bool transport_stopped = false;

            /** @brief Holds how far the handshake has come. */
            LinkPhase phase = LinkPhase::Idle;

            /** @brief Holds when the last frame of the watched connection arrived. */
            std::chrono::steady_clock::time_point last_frame{};

            /** @brief Holds the silence after which the master counts as lost; 0 means no watch. */
            std::chrono::milliseconds liveness{0};

            /** @brief Holds when the next HEARTBEAT is due. */
            std::chrono::steady_clock::time_point heartbeat_at{};

            /** @brief Holds when the newest HELLO went out. */
            std::chrono::steady_clock::time_point hello_at{};

            /** @brief Holds the time between two HEARTBEATs, and the wait for a WELCOME. */
            std::chrono::milliseconds heartbeat_interval{1000};
        };

        /**
         * @brief Holds what is due, and the state the thread keeps after doing it.
         */
        struct LinkTick
        {
            /** @brief Holds what to do now. */
            LinkAction action = LinkAction::Wait;

            /** @brief Holds when to look again after a Wait; nothing means wait for a change. */
            std::optional<std::chrono::steady_clock::time_point> wake;

            /** @brief Holds the phase after the action. */
            LinkPhase phase = LinkPhase::Idle;

            /** @brief Holds whether the transport is stopped after the action. */
            bool transport_stopped = false;

            /** @brief Holds when the next HEARTBEAT is due after the action. */
            std::chrono::steady_clock::time_point heartbeat_at{};

            /** @brief Holds when the newest HELLO went out after the action. */
            std::chrono::steady_clock::time_point hello_at{};

            /** @brief Holds the silence that CloseSilent found. */
            std::chrono::milliseconds silence{0};

            /** @brief Holds the limit that CloseSilent applied. */
            std::chrono::milliseconds limit{0};
        };

        /**
         * @brief Decides what the link's thread does at @p now.
         *
         * A link that ended stops its transport once. A registered link closes a master silent
         * for the liveness timeout, else sends a HEARTBEAT when one is due. A link in its
         * handshake sends HELLO again when no WELCOME came within one interval.
         */
        [[nodiscard]] SUSHIRUNTIME_API LinkTick plan_link_tick(
            const LinkTickInput& input, std::chrono::steady_clock::time_point now) noexcept;

    } // namespace Distributed
} // namespace SushiRuntime
