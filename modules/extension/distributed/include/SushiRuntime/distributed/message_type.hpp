/**************************************************************************/
/* message_type.hpp                                                       */
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
 * @file message_type.hpp
 * @brief Declares MessageType, the type byte of a version 3 distributed frame.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace SushiRuntime::Distributed
{
    /**
     * @brief Names the sixteen messages of protocol version 3 by their type byte.
     */
    enum class MessageType : std::uint8_t
    {
        Hello = 1,     /**< Worker to master: nonce, build id and node name. */
        Challenge,     /**< Master to worker: nonce, proof and session id. */
        Proof,         /**< Worker to master: proof, kernels and capacity. */
        Welcome,       /**< Master to worker: worker id and the session's timings. */
        Reject,        /**< Either way: the handshake is refused, with the cause. */
        Goodbye,       /**< Either way: the sender is leaving, with the cause. */
        TaskDispatch,  /**< Master to worker: run this kernel on these buffers. */
        TaskAccepted,  /**< Worker to master: the dispatch arrived. */
        TaskResult,    /**< Worker to master: the task ended, with its status. */
        BufferRequest, /**< Worker to master: send the bytes of this buffer version. */
        BufferChunk,   /**< Either way: one piece of a buffer's bytes. */
        BufferAbort,   /**< Sender to receiver: the transfer of this buffer stops. */
        BufferRelease, /**< Master to worker: drop this buffer from the cache. */
        Ping,          /**< Master to worker: round-trip probe carrying a token. */
        Pong,          /**< Worker to master: the token back, with free memory. */
        Heartbeat,     /**< Worker to master: queue depth, oldest task and cache size. */
    };

    /** @brief Holds the smallest type byte a frame may carry. */
    inline constexpr std::uint8_t K_FIRST_MESSAGE_TYPE = 1;

    /** @brief Holds the largest type byte a frame may carry. */
    inline constexpr std::uint8_t K_LAST_MESSAGE_TYPE = 16;

    /** @brief Holds the number of message types. */
    inline constexpr std::size_t K_MESSAGE_TYPE_COUNT = 16;

    /** @brief Returns whether a byte read from a frame names a message type. */
    [[nodiscard]] constexpr bool is_message_type(std::uint8_t value) noexcept
    {
        return value >= K_FIRST_MESSAGE_TYPE && value <= K_LAST_MESSAGE_TYPE;
    }

    /**
     * @brief Returns the upper-case name of a message type for a log line.
     * @return "UNKNOWN" for a value outside the enumeration.
     */
    [[nodiscard]] constexpr const char* message_type_name(MessageType type) noexcept
    {
        constexpr const char* K_NAMES[K_MESSAGE_TYPE_COUNT + 1] = {
            "UNKNOWN",        "HELLO",          "CHALLENGE",     "PROOF",       "WELCOME",
            "REJECT",         "GOODBYE",        "TASK_DISPATCH", "TASK_ACCEPTED",
            "TASK_RESULT",    "BUFFER_REQUEST", "BUFFER_CHUNK",  "BUFFER_ABORT",
            "BUFFER_RELEASE", "PING",           "PONG",          "HEARTBEAT",
        };
        const std::uint8_t value = static_cast<std::uint8_t>(type);
        return K_NAMES[is_message_type(value) ? value : 0];
    }

    /**
     * @brief Holds the private codec of protocol version 3; these names reach it unqualified.
     */
    namespace Wire
    {
        using Distributed::is_message_type;
        using Distributed::K_FIRST_MESSAGE_TYPE;
        using Distributed::K_LAST_MESSAGE_TYPE;
        using Distributed::K_MESSAGE_TYPE_COUNT;
        using Distributed::message_type_name;
        using Distributed::MessageType;
    } // namespace Wire
} // namespace SushiRuntime::Distributed
