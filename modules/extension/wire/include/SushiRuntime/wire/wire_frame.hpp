/**************************************************************************/
/* wire_frame.hpp                                                         */
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
 * @file wire_frame.hpp
 * @brief Declares FrameHeader, the 12-byte header in front of every version 4 payload.
 * @author Mustafa Garip
 * @see modules/extension/wire/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>

#include <SushiRuntime/transport/message_type.hpp>

#include <SushiRuntime/wire/wire_codec.hpp>

namespace SushiRuntime::Distributed::Wire
{
    /** @brief Holds the four bytes every frame opens with: 'S', 'R', 'D', 'P'. */
    inline constexpr std::array<std::byte, 4> K_FRAME_MAGIC = {
        std::byte{0x53}, std::byte{0x52}, std::byte{0x44}, std::byte{0x50}};

    /** @brief Holds the protocol version this build writes and the only one it reads. */
    inline constexpr std::uint16_t K_PROTOCOL_VERSION = 4;

    /** @brief Holds the size of the frame header in bytes. */
    inline constexpr std::size_t K_FRAME_HEADER_BYTES = 12;

    /** @brief Holds the largest payload a frame may carry: 1 MiB. */
    inline constexpr std::uint32_t K_MAX_FRAME_PAYLOAD = 1024u * 1024u;

    /** @brief Holds the flag bit that marks the last chunk of a buffer transfer. */
    inline constexpr std::uint8_t K_FRAME_FLAG_LAST_CHUNK = 0x01;

    /**
     * @brief Names the outcome of decoding a frame header, in the order the checks run.
     */
    enum class FrameStatus : std::uint8_t
    {
        Ok,              /**< The header is a version 4 header this build accepts. */
        Truncated,       /**< Fewer than 12 bytes were given. */
        BadMagic,        /**< The first four bytes are not the magic. */
        VersionMismatch, /**< The magic is right and the version is not this build's. */
        UnknownType,     /**< The type byte names no message. */
        ReservedFlags,   /**< A flag bit is set that this type may not carry. */
        PayloadTooLong,  /**< The payload length exceeds K_MAX_FRAME_PAYLOAD. */
    };

    /**
     * @brief Holds the fields of a frame header after the magic.
     */
    struct FrameHeader
    {
        /** @brief Holds the sender's protocol version. */
        std::uint16_t version = K_PROTOCOL_VERSION;

        /** @brief Holds the type of the payload. */
        MessageType type = MessageType::Hello;

        /** @brief Holds the flag bits; only K_FRAME_FLAG_LAST_CHUNK is defined. */
        std::uint8_t flags = 0;

        /** @brief Holds the number of payload bytes that follow the header. */
        std::uint32_t payload_length = 0;
    };

    /** @brief Appends the 12 bytes of a header to a buffer. */
    SUSHIRUNTIME_API void append_frame_header(const FrameHeader& header,
                                              std::vector<std::byte>& out);

    /**
     * @brief Decodes and checks the first 12 bytes of a frame.
     *
     * On VersionMismatch, out.version holds the sender's version and the other fields are
     * left alone; on any other failure out is left alone.
     */
    [[nodiscard]] SUSHIRUNTIME_API FrameStatus
    decode_frame_header(SushiRuntime::span<const std::byte> bytes, FrameHeader& out) noexcept;

    /**
     * @brief Refuses a peer that announced another protocol version than this build's.
     * @param announced The version a frame header of the peer carries.
     * @return Nothing for K_PROTOCOL_VERSION, or ProtocolMismatch naming both versions.
     */
    [[nodiscard]] SUSHIRUNTIME_API Result<void> check_protocol_version(std::uint16_t announced);

    /** @brief Writes a payload length into the header that starts at header_offset. */
    SUSHIRUNTIME_API void patch_payload_length(std::vector<std::byte>& frame,
                                               std::size_t header_offset,
                                               std::uint32_t payload_length) noexcept;

    /**
     * @brief Appends one whole frame, header then payload, for a message.
     *
     * On failure the buffer is cut back to the size it had on entry.
     *
     * @tparam Message A wire message: it names its type as K_TYPE and has encode().
     */
    template <typename Message>
    [[nodiscard]] CodecStatus append_frame(const Message& message, std::uint8_t flags,
                                           std::vector<std::byte>& out)
    {
        const std::size_t header_offset = out.size();
        append_frame_header(FrameHeader{K_PROTOCOL_VERSION, Message::K_TYPE, flags, 0}, out);

        CodecStatus status = message.encode(out);
        const std::size_t payload_length = out.size() - header_offset - K_FRAME_HEADER_BYTES;
        if (status == CodecStatus::Ok && payload_length > K_MAX_FRAME_PAYLOAD)
            status = CodecStatus::TooLong;

        if (status != CodecStatus::Ok)
        {
            out.resize(header_offset);
            return status;
        }
        patch_payload_length(out, header_offset, static_cast<std::uint32_t>(payload_length));
        return CodecStatus::Ok;
    }
} // namespace SushiRuntime::Distributed::Wire
