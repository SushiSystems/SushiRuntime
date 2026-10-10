/**************************************************************************/
/* peer_connection.cpp                                                    */
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
 * @file peer_connection.cpp
 * @brief Runs the reader and the writer of one TCP connection.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "peer_connection.hpp"

#include <array>
#include <exception>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <SushiRuntime/logging/logger.hpp>

#include "wire_frame.hpp"
#include "wire_messages.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the largest payload the writer copies behind its header. */
            constexpr std::size_t K_COALESCED_PAYLOAD_BYTES = 4096;

            /** @brief Holds the offset of the type byte inside a frame header. */
            constexpr std::size_t K_TYPE_OFFSET = 6;

            /** @brief Holds the offset of the flag byte inside a frame header. */
            constexpr std::size_t K_FLAGS_OFFSET = 7;

            /** @brief Holds the offset of the payload length inside a frame header. */
            constexpr std::size_t K_LENGTH_OFFSET = 8;

            /** @brief Holds the header bytes one frame opens with. */
            using HeaderBytes = std::array<std::byte, Wire::K_FRAME_HEADER_BYTES>;

            /** @brief Formats bytes as `0x` and two lower-case hex digits each. */
            std::string to_hex(SushiRuntime::span<const std::byte> bytes)
            {
                constexpr char K_DIGITS[] = "0123456789abcdef";
                std::string text("0x");
                for (const std::byte value : bytes)
                {
                    const unsigned number = std::to_integer<unsigned>(value);
                    text.push_back(K_DIGITS[number >> 4]);
                    text.push_back(K_DIGITS[number & 0x0Fu]);
                }
                return text;
            }

            /** @brief Reads the little-endian payload length of a header that failed its check. */
            std::uint32_t raw_payload_length(const HeaderBytes& header) noexcept
            {
                std::uint32_t length = 0;
                for (std::size_t index = 0; index < sizeof(length); ++index)
                    length |= std::to_integer<std::uint32_t>(header[K_LENGTH_OFFSET + index])
                              << (8 * index);
                return length;
            }

            /** @brief Describes a read that ended a connection. */
            PeerLoss loss_of_read(const SocketResult& read)
            {
                PeerLoss loss;
                if (read.status == SocketStatus::Closed)
                {
                    loss.cause = PeerLossCause::ClosedByPeer;
                    return loss;
                }
                loss.cause = PeerLossCause::IoFailed;
                loss.text = describe(read);
                return loss;
            }

            /**
             * @brief Describes a header that failed decode_frame_header().
             * @param decoded Holds the sender's version after VersionMismatch.
             */
            PeerLoss loss_of_header(Wire::FrameStatus status, const Wire::FrameHeader& decoded,
                                    const HeaderBytes& header)
            {
                PeerLoss loss;
                const std::string type_byte = to_hex({header.data() + K_TYPE_OFFSET, 1});
                switch (status)
                {
                    case Wire::FrameStatus::BadMagic:
                        loss.cause = PeerLossCause::BadMagic;
                        loss.text = "first four " + to_hex({header.data(), 4});
                        return loss;
                    case Wire::FrameStatus::VersionMismatch:
                        loss.cause = PeerLossCause::VersionMismatch;
                        loss.peer_version = decoded.version;
                        return loss;
                    case Wire::FrameStatus::UnknownType:
                        loss.text = "type byte " + type_byte + " names no message";
                        break;
                    case Wire::FrameStatus::ReservedFlags:
                        loss.text = "flags " + to_hex({header.data() + K_FLAGS_OFFSET, 1}) +
                                    " are reserved for type byte " + type_byte;
                        break;
                    case Wire::FrameStatus::PayloadTooLong:
                        loss.text = "payload length " +
                                    std::to_string(raw_payload_length(header)) + " exceeds " +
                                    std::to_string(Wire::K_MAX_FRAME_PAYLOAD) +
                                    " in a frame of type byte " + type_byte;
                        break;
                    case Wire::FrameStatus::Ok:
                    case Wire::FrameStatus::Truncated:
                        loss.text = "the header is shorter than 12 bytes";
                        break;
                }
                loss.cause = PeerLossCause::MalformedFrame;
                return loss;
            }

            /** @brief Queues the REJECT that tells a peer of another version why it is closed. */
            void queue_version_reject(Outbox& outbox, const PeerInfo& info)
            {
                const std::string text = "the receiver speaks protocol v" +
                                         std::to_string(Wire::K_PROTOCOL_VERSION) +
                                         "; build both ends from one commit";
                Wire::RejectMessage reject;
                reject.reason = Wire::RejectReason::Version;
                reject.sender_version = Wire::K_PROTOCOL_VERSION;
                reject.text = text;

                std::vector<std::byte> payload;
                if (reject.encode(payload) != Wire::CodecStatus::Ok)
                    return;
                const SendStatus queued =
                    outbox.push_control(MessageType::Reject, {payload.data(), payload.size()});
                if (queued != SendStatus::Queued)
                    SR_LOG_DEBUG("{} gets no REJECT(version): its control queue refused the frame",
                                 info.endpoint);
            }

            /** @brief Logs an exception that escaped an event; the peer ends, not the process. */
            void log_handler_failure(PeerId peer, const PeerInfo& info, const char* event,
                                     std::size_t length, const char* what)
            {
                SR_LOG_ERROR("handler of {} ({} bytes) from {} (peer {}) threw '{}'; "
                             "closing the peer, the process continues",
                             event, length, info.endpoint, peer.value, what);
            }

            /**
             * @brief Makes on_peer_connected() and catches what escapes it.
             * @return False when the event threw; @p loss then holds HandlerFailed.
             */
            bool announce_peer(TransportEvents& events, PeerId peer, const PeerInfo& info,
                               PeerLoss& loss) noexcept
            {
                try
                {
                    events.on_peer_connected(peer, info);
                    return true;
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(peer, info, "the connection event", 0, error.what());
                    loss.text = error.what();
                }
                catch (...)
                {
                    log_handler_failure(peer, info, "the connection event", 0, "unknown");
                    loss.text = "an exception that is no std::exception";
                }
                loss.cause = PeerLossCause::HandlerFailed;
                return false;
            }

            /**
             * @brief Makes on_frame() and catches what escapes it.
             * @return False when the event threw; @p loss then holds HandlerFailed.
             */
            bool deliver_frame(TransportEvents& events, PeerId peer, const PeerInfo& info,
                               const Wire::FrameHeader& header,
                               SushiRuntime::span<const std::byte> payload,
                               PeerLoss& loss) noexcept
            {
                const char* name = message_type_name(header.type);
                try
                {
                    events.on_frame(peer, header.type, header.flags, payload);
                    return true;
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(peer, info, name, payload.size(), error.what());
                    loss.text = error.what();
                }
                catch (...)
                {
                    log_handler_failure(peer, info, name, payload.size(), "unknown");
                    loss.text = "an exception that is no std::exception";
                }
                loss.cause = PeerLossCause::HandlerFailed;
                return false;
            }

            /** @brief Makes on_peer_lost() and logs what escapes it. */
            void report_loss(TransportEvents& events, PeerId peer, const PeerInfo& info,
                             const PeerLoss& loss) noexcept
            {
                try
                {
                    events.on_peer_lost(peer, loss);
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(peer, info, "the loss event", 0, error.what());
                }
                catch (...)
                {
                    log_handler_failure(peer, info, "the loss event", 0, "unknown");
                }
            }
        } // namespace

        PeerConnection::PeerConnection(PeerId peer, Socket socket, PeerInfo info,
                                       const PeerConnectionOptions& options,
                                       TransportEvents& events)
            : peer_(peer), socket_(std::move(socket)), info_(std::move(info)), options_(options),
              events_(events), outbox_(options.control_queue_frames)
        {

        }

        PeerConnection::~PeerConnection()
        {
            stop();
        }

        void PeerConnection::start()
        {
            running_threads_.store(2);
            try
            {
                writer_ = std::thread([this] { write_loop(); });
            }
            catch (const std::system_error& error)
            {
                running_threads_.store(0);
                SR_LOG_ERROR("cannot start the writer thread for {}: {}; the connection is "
                             "dropped",
                             info_.endpoint, error.what());
                return;
            }

            try
            {
                reader_ = std::thread([this] { read_loop(); });
            }
            catch (const std::system_error& error)
            {
                running_threads_.fetch_sub(1);
                SR_LOG_ERROR("cannot start the reader thread for {}: {}; the connection is "
                             "dropped",
                             info_.endpoint, error.what());
                outbox_.close();
            }
        }

        SendStatus PeerConnection::send(MessageType type,
                                        SushiRuntime::span<const std::byte> payload)
        {
            return outbox_.push_control(type, payload);
        }

        SendStatus PeerConnection::send_stream(std::unique_ptr<FrameSource> source)
        {
            return outbox_.push_stream(std::move(source));
        }

        void PeerConnection::close() noexcept
        {
            closed_locally_.store(true);
            outbox_.close();

            // A healthy write returns and lingers; this one would hold the loss back.
            if (has_spent_linger())
                socket_.shutdown();
        }

        void PeerConnection::stop() noexcept
        {
            stopping_.store(true);
            outbox_.close();
            socket_.shutdown();
            if (reader_.joinable())
                reader_.join();
            if (writer_.joinable())
                writer_.join();
        }

        bool PeerConnection::is_finished() const noexcept
        {
            return running_threads_.load() == 0;
        }

        PeerId PeerConnection::peer() const noexcept
        {
            return peer_;
        }

        std::uint64_t PeerConnection::bytes_sent() const noexcept
        {
            return bytes_sent_.load(std::memory_order_relaxed);
        }

        std::uint64_t PeerConnection::bytes_received() const noexcept
        {
            return bytes_received_.load(std::memory_order_relaxed);
        }

        void PeerConnection::read_loop() noexcept
        {
            PeerLoss loss;
            HeaderBytes header_bytes{};
            std::vector<std::byte> payload;

            bool is_open = !stopping_.load() && announce_peer(events_, peer_, info_, loss);
            while (is_open)
            {
                SocketResult read = socket_.read_exact(
                    {header_bytes.data(), header_bytes.size()}, K_WAIT_FOREVER);
                bytes_received_.fetch_add(read.transferred, std::memory_order_relaxed);

                Wire::FrameHeader header;
                Wire::FrameStatus checked = Wire::FrameStatus::Ok;
                if (read.is_ok())
                {
                    checked = Wire::decode_frame_header(
                        {header_bytes.data(), header_bytes.size()}, header);
                    if (checked == Wire::FrameStatus::VersionMismatch)
                        queue_version_reject(outbox_, info_);
                }
                if (read.is_ok() && checked != Wire::FrameStatus::Ok)
                {
                    loss = loss_of_header(checked, header, header_bytes);
                    break;
                }

                // header.payload_length passed the 1 MiB check, so the resize is bounded.
                if (read.is_ok() && header.payload_length > 0)
                {
                    payload.resize(header.payload_length);
                    read = socket_.read_exact({payload.data(), payload.size()}, K_WAIT_FOREVER);
                    bytes_received_.fetch_add(read.transferred, std::memory_order_relaxed);
                }
                if (!read.is_ok())
                {
                    loss = loss_of_read(read);
                    if (write_stalled_.load())
                        loss = PeerLoss{PeerLossCause::WriteStalled, 0,
                                        "no byte left for " +
                                            std::to_string(options_.write_idle_timeout.count()) +
                                            " ms"};
                    else if (closed_locally_.load())
                        loss = PeerLoss{PeerLossCause::ClosedLocally, 0, std::string()};
                    break;
                }

                is_open = deliver_frame(events_, peer_, info_, header,
                                        {payload.data(), header.payload_length}, loss);
            }

            // The cause is settled above; from here the flag only starts the writer's linger.
            close();
            if (!stopping_.load())
                report_loss(events_, peer_, info_, loss);
            running_threads_.fetch_sub(1);
        }

        void PeerConnection::write_loop() noexcept
        {
            using Clock = std::chrono::steady_clock;

            OutboundFrame frame;
            std::vector<std::byte> head;
            head.reserve(Wire::K_FRAME_HEADER_BYTES + K_COALESCED_PAYLOAD_BYTES);
            bool is_lingering = false;
            Clock::time_point linger_end;

            while (outbox_.pop(frame, K_WAIT_FOREVER) == PopStatus::Frame && !stopping_.load())
            {
                std::chrono::milliseconds limit = options_.write_idle_timeout;
                if (closed_locally_.load())
                {
                    const Clock::time_point now = Clock::now();
                    if (!is_lingering)
                        linger_end = now + options_.close_linger;
                    is_lingering = true;
                    if (now >= linger_end)
                        break;
                    const std::chrono::milliseconds left =
                        std::chrono::duration_cast<std::chrono::milliseconds>(linger_end - now);
                    if (limit.count() < 0 || left < limit)
                        limit = left;
                }

                head.clear();
                Wire::append_frame_header(
                    Wire::FrameHeader{Wire::K_PROTOCOL_VERSION, frame.type, frame.flags,
                                      static_cast<std::uint32_t>(frame.payload.size())},
                    head);
                const bool is_coalesced = frame.payload.size() <= K_COALESCED_PAYLOAD_BYTES;
                if (is_coalesced)
                    head.insert(head.end(), frame.payload.begin(), frame.payload.end());

                SocketResult written = write_timed({head.data(), head.size()}, limit);
                if (written.is_ok() && !is_coalesced)
                    written = write_timed({frame.payload.data(), frame.payload.size()}, limit);
                if (written.is_ok())
                    continue;

                if (written.status == SocketStatus::Timeout && !is_lingering)
                    write_stalled_.store(true);
                break;
            }

            outbox_.close();
            socket_.shutdown();
            running_threads_.fetch_sub(1);
        }

        SocketResult PeerConnection::write_timed(SushiRuntime::span<const std::byte> bytes,
                                                 std::chrono::milliseconds limit)
        {
            using Clock = std::chrono::steady_clock;

            write_began_ticks_.store(Clock::now().time_since_epoch().count(),
                                     std::memory_order_release);
            SocketResult written = socket_.write_all(bytes, limit);
            write_began_ticks_.store(0, std::memory_order_release);

            bytes_sent_.fetch_add(written.transferred, std::memory_order_relaxed);
            return written;
        }

        bool PeerConnection::has_spent_linger() const noexcept
        {
            using Clock = std::chrono::steady_clock;

            const Clock::rep began = write_began_ticks_.load(std::memory_order_acquire);
            if (began == 0)
                return false;
            return Clock::now() - Clock::time_point(Clock::duration(began)) >=
                   options_.close_linger;
        }

    } // namespace Distributed
} // namespace SushiRuntime
