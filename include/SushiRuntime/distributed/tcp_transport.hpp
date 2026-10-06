/**************************************************************************/
/* tcp_transport.hpp                                                      */
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
 * @file tcp_transport.hpp
 * @brief Declares TcpTransport, the ITransport implementation over TCP sockets.
 * @author Mustafa Garip
 */

#pragma once

#include <string>
#include <memory>
#include <cstdint>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Provides a TCP-based implementation for network communication using raw sockets.
         *
         * Establishes asymmetric master-worker connections to exchange framed messages across network boundaries.
         */
        class SUSHIRUNTIME_API TcpTransport final : public ITransport
        {
            public:
                /**
                 * @brief Creates a master endpoint bound to port; accepting waits until start().
                 * @param port The port to bind, or 0 to let the operating system assign one.
                 * @param worker_count The exact number of worker connections to wait for.
                 * @throws std::runtime_error If socket creation or binding fails.
                 */
                static std::shared_ptr<TcpTransport> make_master(uint16_t port, int worker_count);

                /**
                 * @brief Creates a worker endpoint that connects to the master when start() is
                 *        called.
                 */
                static std::shared_ptr<TcpTransport> make_worker(const std::string& host, uint16_t port);

                /**
                 * @brief Cleans up network resources and shuts down active connections.
                 *
                 * Ensures all underlying socket handles are properly closed and worker threads are joined.
                 */
                ~TcpTransport() override;

                /**
                 * @brief Frames a binary payload with a header and writes it synchronously to a
                 *        peer.
                 * @param peer_id The target peer, or < 0 to broadcast to all peers (master only).
                 */
                void send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload) override;

                /**
                 * @brief Registers a callback function to handle incoming network messages.
                 *
                 * Stores the provided handler to be invoked whenever a complete frame is received.
                 *
                 * @param handler The callback function to execute on message arrival.
                 */
                void on_message(MessageHandler handler) override;

                /**
                 * @brief Activates the transport layer to begin network operations.
                 *
                 * Starts background threads for accepting connections (master) or connecting and reading data (worker).
                 */
                void start() override;

                /**
                 * @brief Terminates all active network operations and gracefully stops background threads.
                 *
                 * Signals the internal loops to exit and closes open sockets to force unblocking.
                 */
                void stop() override;

                /**
                 * @brief Retrieves the total number of connected peers.
                 *
                 * Returns the count of established connections managed by this transport instance.
                 *
                 * @return The integer count of active peer connections.
                 */
                int  peer_count() const override;

                /**
                 * @brief Retrieves the actual local port number bound by a master endpoint.
                 *
                 * Queries the underlying socket address to determine the OS-assigned port if 0 was originally requested.
                 *
                 * @return The local port number in use.
                 * @warning This function is only valid for master endpoints; calling it on a worker yields undefined results.
                 */
                uint16_t bound_port() const;

                struct Impl;

            private:
                explicit TcpTransport(std::shared_ptr<Impl> impl);
                std::shared_ptr<Impl> impl_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
