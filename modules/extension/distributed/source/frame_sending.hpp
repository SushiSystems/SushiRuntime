/**************************************************************************/
/* frame_sending.hpp                                                      */
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
 * @file frame_sending.hpp
 * @brief Declares send_message(), which encodes a wire message and queues it as a control frame.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <vector>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/transport.hpp>

#include "wire_codec.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Encodes a message into a buffer the caller reuses and queues it for a peer.
         * @tparam Message A wire message: it names its type as K_TYPE and has encode().
         * @param scratch Cleared first; its capacity is kept, so a warm buffer allocates nothing.
         * @return TooLong when the message does not encode, else what Transport::send() said.
         */
        template <typename Message>
        [[nodiscard]] SendStatus send_message(Transport& transport, PeerID peer,
                                              const Message& message,
                                              std::vector<std::byte>& scratch)
        {
            scratch.clear();
            if (message.encode(scratch) != Wire::CodecStatus::Ok)
                return SendStatus::TooLong;

            return transport.send(
                peer, Message::K_TYPE,
                SushiRuntime::span<const std::byte>(scratch.data(), scratch.size()));
        }

        /**
         * @brief Encodes a message into a buffer of its own and queues it for a peer.
         * @tparam Message A wire message: it names its type as K_TYPE and has encode().
         * @return TooLong when the message does not encode, else what Transport::send() said.
         */
        template <typename Message>
        [[nodiscard]] SendStatus send_message(Transport& transport, PeerID peer,
                                              const Message& message)
        {
            std::vector<std::byte> payload;
            return send_message(transport, peer, message, payload);
        }

    } // namespace Distributed
} // namespace SushiRuntime
