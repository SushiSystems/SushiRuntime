/**************************************************************************/
/* i_transport.hpp                                                        */
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
 * @file i_transport.hpp
 * @brief Declares ITransport, the message-passing interface of the cluster.
 * @author Mustafa Garip
 */

#pragma once

#include <SushiRuntime/core/span.hpp>
#include <functional>
#include <cstddef>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/protocol.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Hardware-agnostic interface for message passing across the cluster.
         *
         * Decouples the coordination logic from the underlying network topology or local simulation queues.
         */
        class SUSHIRUNTIME_API ITransport
        {
            public:
                /**
                 * @brief Callback invoked when a fully framed message arrives.
                 *
                 * @param peer The ID of the sending node.
                 * @param type The enumerated kind of the received message.
                 * @param payload The raw serialized bytes of the message body.
                 */
                using MessageHandler =
                    std::function<void(int peer, MsgType type, SushiRuntime::span<const std::byte> payload)>;

                virtual ~ITransport() = default;

                /**
                 * @brief Transmits a framed message to a target peer.
                 *
                 * @param peer_id The destination node ID. Use a negative value for broadcast from the master.
                 * @param type The semantic kind of the message.
                 * @param payload The serialized data to transmit.
                 */
                virtual void send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload) = 0;

                /**
                 * @brief Registers the callback responsible for handling all inbound messages.
                 *
                 * @param handler The function to invoke upon message receipt.
                 */
                virtual void on_message(MessageHandler handler) = 0;

                /**
                 * @brief Activates the transport layer to begin network operations.
                 *
                 * Opens sockets, accepts connections, and starts I/O worker threads.
                 */
                virtual void start() = 0;

                /**
                 * @brief Halts the transport layer and releases network resources.
                 *
                 * Safely shuts down I/O threads and closes connections. Safe to call multiple times.
                 */
                virtual void stop() = 0;

                /**
                 * @brief Retrieves the number of reachable endpoints.
                 *
                 * @return The worker count for the master, or 1 for a worker node.
                 */
                virtual int peer_count() const = 0;
        };

    } // namespace Distributed
} // namespace SushiRuntime
