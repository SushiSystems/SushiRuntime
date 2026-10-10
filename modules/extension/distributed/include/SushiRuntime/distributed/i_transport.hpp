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
 * @brief Declares ITransport, the frame-passing seam between a session and its peers.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Names one connection of a transport.
         *
         * A transport numbers its connections from 1 and never reuses a number, so a peer
         * that reconnects is a new peer. On a worker the only peer is the master.
         */
        struct PeerId
        {
            /** @brief Holds the number; 0 names no peer. */
            std::uint32_t value = 0;

            /** @brief Returns whether two ids name the same connection. */
            [[nodiscard]] constexpr bool operator==(const PeerId& other) const noexcept
            {
                return value == other.value;
            }

            /** @brief Returns whether two ids name different connections. */
            [[nodiscard]] constexpr bool operator!=(const PeerId& other) const noexcept
            {
                return value != other.value;
            }

            /** @brief Returns whether the id names a connection. */
            [[nodiscard]] constexpr bool valid() const noexcept
            {
                return value != 0;
            }
        };

        /**
         * @brief Names what send() and send_stream() did with a frame.
         */
        enum class SendStatus : std::uint8_t
        {
            Queued,      /**< The frame is in the peer's queue; delivery is not promised. */
            QueueFull,   /**< The peer's control queue is at its bound; nothing was queued. */
            UnknownPeer, /**< The id names no open connection: never seen, closed or lost. */
            TooLong,     /**< The payload exceeds what one frame carries (1 MiB). */
            Stopped,     /**< The transport is not started, or stop() has begun. */
        };

        /**
         * @brief Names how start() ended.
         */
        enum class TransportStatus : std::uint8_t
        {
            Ok,              /**< The transport runs; events may arrive from this point. */
            AlreadyStarted,  /**< start() was called before; a transport starts once. */
            InvalidArgument, /**< An option rules the start out; the message says which. */
            BindFailed,      /**< The master could not listen on its address. */
            Failed,          /**< The platform refused something else; the message says what. */
        };

        /**
         * @brief Carries the outcome of ITransport::start().
         */
        struct TransportResult
        {
            /** @brief How the call ended. */
            TransportStatus status = TransportStatus::Ok;

            /**
             * @brief Names the endpoint and the cause for a log line.
             *
             * On Ok it holds a master's bound endpoint and is empty for a worker.
             */
            std::string message;

            /** @brief Returns whether the transport started. */
            [[nodiscard]] bool is_ok() const noexcept
            {
                return status == TransportStatus::Ok;
            }
        };

        /**
         * @brief Names who can reach a transport, which decides whether a secret is required.
         */
        enum class TransportExposure : std::uint8_t
        {
            Loopback, /**< Only this machine: an in-process fabric or a loopback address. */
            Network,  /**< Other machines can connect, or this end connects to one. */
        };

        /**
         * @brief Names why a connection ended.
         */
        enum class PeerLossCause : std::uint8_t
        {
            ClosedByPeer,    /**< The other end closed the connection. */
            ClosedLocally,   /**< close_peer() was called for it. */
            IoFailed,        /**< A read or a write failed; the text carries the OS reason. */
            WriteStalled,    /**< No byte left for the transport's write idle timeout. */
            BadMagic,        /**< The peer sent bytes that are not a frame. */
            VersionMismatch, /**< The peer's frames carry another protocol version. */
            MalformedFrame,  /**< A header named no type, a reserved flag or too long a payload. */
            HandlerFailed,   /**< TransportEvents::on_frame() let an exception escape. */
        };

        /**
         * @brief Describes the other end of a connection when it opens.
         */
        struct PeerInfo
        {
            /** @brief Holds the numeric `address:port` of the peer, or the fabric's name for it. */
            std::string endpoint;
        };

        /**
         * @brief Describes why a connection ended, with what a log line needs.
         */
        struct PeerLoss
        {
            /** @brief Holds the cause. */
            PeerLossCause cause = PeerLossCause::ClosedByPeer;

            /** @brief Holds the peer's protocol version after VersionMismatch, otherwise 0. */
            std::uint16_t peer_version = 0;

            /** @brief Holds the OS reason or the offending bytes in words; may be empty. */
            std::string text;
        };

        /**
         * @brief Counts the bytes a transport moved since it started, headers included.
         */
        struct TransportStatistics
        {
            std::uint64_t bytes_sent = 0;     /**< Bytes written to every peer. */
            std::uint64_t bytes_received = 0; /**< Bytes read from every peer. */
        };

        /**
         * @brief Holds one frame on its way out: type, flags and payload.
         *
         * The writer that pulls a FrameSource owns one and passes it to every next() call, so
         * the payload's capacity is reused.
         */
        struct OutboundFrame
        {
            /** @brief Holds the type of the payload. */
            MessageType type = MessageType::Hello;

            /** @brief Holds the header's flag byte; bit 0 marks the last chunk of a transfer. */
            std::uint8_t flags = 0;

            /** @brief Holds the payload, at most 1 MiB. */
            std::vector<std::byte> payload;
        };

        /**
         * @brief Names what FrameSource::next() produced.
         */
        enum class FrameSourceStatus : std::uint8_t
        {
            Frame,      /**< A frame was written; the source has more. */
            FinalFrame, /**< A frame was written and it is the source's last. */
            Finished,   /**< Nothing was written; the source is done. */
        };

        /**
         * @brief Produces the frames of one bulk transfer, one per call, when the peer can take it.
         *
         * The transport owns the source from send_stream() on and destroys it after the last
         * frame, when the peer closes or in stop(), on whichever of its threads gets there.
         */
        class FrameSource
        {
            public:
                /** @brief Ends the transfer; may run on any transport thread. */
                virtual ~FrameSource() = default;

                /**
                 * @brief Writes the next frame into @p frame.
                 *
                 * Called by the one thread that writes to the peer, never twice at once. It may
                 * take a short lock and copy one chunk; it does not call the transport, block
                 * on I/O or throw.
                 * @param frame Its payload holds the last frame's bytes; clear or overwrite it.
                 */
                [[nodiscard]] virtual FrameSourceStatus next(OutboundFrame& frame) noexcept = 0;
        };

        /**
         * @brief Receives what a transport observes; the session layer implements it.
         *
         * For one peer the calls are on_peer_connected(), then on_frame() in the order the
         * peer sent, then on_peer_lost() once, never two at a time. Calls for different peers
         * may overlap. A call may use send(), send_stream() and close_peer(), also for its own
         * peer; it calls neither start() nor stop(), and a long call delays that peer's frames.
         */
        class TransportEvents
        {
            public:
                /**
                 * @brief Hears that a connection opened and may be sent to.
                 * @param peer A number this transport has not used before.
                 */
                virtual void on_peer_connected(PeerId peer, const PeerInfo& info) = 0;

                /**
                 * @brief Receives one whole frame whose header the transport has checked.
                 *
                 * An exception that escapes is caught by the transport, which closes the peer
                 * with HandlerFailed and keeps running.
                 * @param payload Valid during the call only; copy what must outlive it.
                 */
                virtual void on_frame(PeerId peer, MessageType type, std::uint8_t flags,
                                      SushiRuntime::span<const std::byte> payload) = 0;

                /**
                 * @brief Hears that a connection ended; no call for @p peer follows.
                 *
                 * Sends to the peer already return UnknownPeer. The transport does not log the
                 * loss; the receiver does. stop() closes peers without this call.
                 */
                virtual void on_peer_lost(PeerId peer, const PeerLoss& loss) = 0;

                /**
                 * @brief Hears that a worker-side transport will not try to connect again.
                 *
                 * Called once: when the time allowed for the first connection ran out, or after
                 * on_peer_lost() when reconnecting is off. No call follows it, and a
                 * master-side transport never makes it.
                 * @param cause The last failure, with the endpoint, for a log line.
                 */
                virtual void on_connect_abandoned(const std::string& cause) = 0;

            protected:
                /** @brief Keeps a receiver from being destroyed through this interface. */
                ~TransportEvents() = default;
        };

        /**
         * @brief Moves frames of protocol version 2 between this process and its peers.
         *
         * One object serves one session: it is started once, by the coordinator or the worker
         * agent that is its only event receiver. send(), send_stream(), close_peer() and
         * statistics() may be called from any thread, several at once, also from an event.
         * @see modules/extension/distributed/README.md
         */
        class SUSHIRUNTIME_API ITransport
        {
            public:
                /** @brief Stops the transport if it still runs. */
                virtual ~ITransport() = default;

                /**
                 * @brief Opens the transport and starts its threads.
                 *
                 * A master binds and listens before this returns, so BindFailed is reported
                 * here. A worker returns Ok at once and connects on its own thread.
                 * @param events Outlives stop(). Calls begin before this returns Ok.
                 * @return AlreadyStarted on every call after the first, with nothing changed.
                 */
                [[nodiscard]] virtual TransportResult start(TransportEvents& events) = 0;

                /**
                 * @brief Closes every peer and joins every thread; blocks until they end.
                 *
                 * No event is in progress and none is made once this returns. A second call
                 * does nothing. A stopped transport does not start again.
                 * @pre The caller is not inside an event of this transport.
                 */
                virtual void stop() noexcept = 0;

                /**
                 * @brief Queues one control frame for a peer, ahead of every stream to it.
                 *
                 * Copies the payload and returns; it never waits for the socket. Control frames
                 * to one peer arrive in the order they were queued.
                 * @param payload At most 1 MiB; the header's flags are 0.
                 */
                [[nodiscard]] virtual SendStatus send(
                    PeerId peer, MessageType type, SushiRuntime::span<const std::byte> payload) = 0;

                /**
                 * @brief Hands a peer's writer a source of bulk frames.
                 *
                 * The writer sends every queued control frame, then one frame of one stream,
                 * taking the peer's streams in turn. On any status but Queued the source is
                 * destroyed before this returns.
                 * @param source Not null. Its frames arrive in the order next() made them.
                 */
                [[nodiscard]] virtual SendStatus send_stream(
                    PeerId peer, std::unique_ptr<FrameSource> source) = 0;

                /**
                 * @brief Ends one connection without waiting for it.
                 *
                 * Sends to the peer return UnknownPeer from here on. Streams are dropped and
                 * control frames already queued are written for a bounded time, then
                 * on_peer_lost() reports ClosedLocally; a peer that takes no byte may be reported
                 * later, as WriteStalled. An unknown id is ignored.
                 */
                virtual void close_peer(PeerId peer) noexcept = 0;

                /**
                 * @brief Returns who can reach this transport; valid before start().
                 */
                [[nodiscard]] virtual TransportExposure exposure() const noexcept = 0;

                /**
                 * @brief Returns the bytes moved so far; it may take a short lock.
                 */
                [[nodiscard]] virtual TransportStatistics statistics() const noexcept = 0;
        };

    } // namespace Distributed
} // namespace SushiRuntime
