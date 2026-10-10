/**************************************************************************/
/* peer_connection.hpp                                                    */
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
 * @file peer_connection.hpp
 * @brief Declares PeerConnection, one TCP connection with its reader, writer and outbox.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "outbox.hpp"
#include "socket.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds the settings TcpTransport gives each connection.
         */
        struct PeerConnectionOptions
        {
            /** @brief Most control frames the outbox queues. */
            std::size_t control_queue_frames = 4096;

            /** @brief Ends the connection with WriteStalled when no byte leaves for this long. */
            std::chrono::milliseconds write_idle_timeout{5000};

            /** @brief Bounds how long close() keeps writing queued control frames. */
            std::chrono::milliseconds close_linger{1000};
        };

        /**
         * @brief Runs one connected socket: a reader thread, a writer thread and an outbox.
         *
         * The reader checks each 12-byte header, reads the payload into one reused buffer and
         * makes every event of the peer, on_peer_connected() first and on_peer_lost() last,
         * so they never overlap. The connection is the outbox's only popper. It owns no
         * mutex of its own; push, close and the counters may be called from any thread.
         */
        class SUSHIRUNTIME_API PeerConnection final
        {
            public:
                /**
                 * @brief Takes a connected socket; no thread runs until start().
                 * @param peer The id the transport's peer table gave the connection.
                 * @param info What on_peer_connected() reports.
                 * @param events Outlives the connection; in TcpTransport an adapter that also
                 *        updates the peer table, so the table learns of the loss first.
                 */
                PeerConnection(PeerId peer, Socket socket, PeerInfo info,
                               const PeerConnectionOptions& options, TransportEvents& events);

                /** @brief Calls stop(). */
                ~PeerConnection();

                PeerConnection(const PeerConnection&) = delete;
                PeerConnection& operator=(const PeerConnection&) = delete;
                PeerConnection(PeerConnection&&) = delete;
                PeerConnection& operator=(PeerConnection&&) = delete;

                /** @brief Starts the two threads; called once, by the thread that built this. */
                void start();

                /** @brief Queues a control frame; see Outbox::push_control(). */
                [[nodiscard]] SendStatus send(MessageType type,
                                              SushiRuntime::span<const std::byte> payload);

                /** @brief Queues a stream; see Outbox::push_stream(). */
                [[nodiscard]] SendStatus send_stream(std::unique_ptr<FrameSource> source);

                /**
                 * @brief Closes the outbox and returns; the threads end on their own.
                 *
                 * The writer writes the control frames left, for at most close_linger, then
                 * shuts the socket down; the reader then reports ClosedLocally. Callable from
                 * an event of this peer. A second call does nothing.
                 */
                void close() noexcept;

                /**
                 * @brief Shuts the socket down and joins both threads, with no on_peer_lost().
                 * @pre The caller is neither thread of this connection.
                 */
                void stop() noexcept;

                /**
                 * @brief Returns whether both threads have ended, so stop() will not block.
                 *
                 * The transport reaps finished connections from its accept or connect thread.
                 */
                [[nodiscard]] bool is_finished() const noexcept;

                /** @brief Returns the id the connection was built with. */
                [[nodiscard]] PeerId peer() const noexcept;

                /** @brief Returns the bytes written to the socket, headers included. */
                [[nodiscard]] std::uint64_t bytes_sent() const noexcept;

                /** @brief Returns the bytes read from the socket, headers included. */
                [[nodiscard]] std::uint64_t bytes_received() const noexcept;

            private:
                /**
                 * @brief Runs the reader: events, frames, then the loss.
                 *
                 * A header that fails decode_frame_header() ends the connection with BadMagic,
                 * VersionMismatch or MalformedFrame; after VersionMismatch the reader first
                 * queues REJECT(version) and closes the outbox. An exception out of on_frame()
                 * is caught without a rethrow and ends the connection with HandlerFailed.
                 */
                void read_loop() noexcept;

                /** @brief Runs the writer: pops the outbox and writes header, then payload. */
                void write_loop() noexcept;

                /// @brief The id the transport gave the connection.
                PeerId peer_;

                /// @brief The socket both threads use; shutdown() is the only cross-thread call.
                Socket socket_;

                /// @brief What on_peer_connected() reports.
                PeerInfo info_;

                /// @brief The settings the transport gave.
                PeerConnectionOptions options_;

                /// @brief Receives this peer's events.
                TransportEvents& events_;

                /// @brief What the writer is owed.
                Outbox outbox_;

                /// @brief Makes the peer's events and reads its frames.
                std::thread reader_;

                /// @brief Drains the outbox into the socket.
                std::thread writer_;

                /// @brief Counts the threads still running.
                std::atomic<int> running_threads_{0};

                /// @brief Set by close() so the reader reports ClosedLocally.
                std::atomic<bool> closed_locally_{false};

                /// @brief Set by stop() so the reader reports no loss.
                std::atomic<bool> stopping_{false};

                /// @brief Set by the writer so the reader reports WriteStalled.
                std::atomic<bool> write_stalled_{false};

                /// @brief Bytes written to the socket.
                std::atomic<std::uint64_t> bytes_sent_{0};

                /// @brief Bytes read from the socket.
                std::atomic<std::uint64_t> bytes_received_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
