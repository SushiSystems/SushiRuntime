/**************************************************************************/
/* wire_frame.cpp                                                         */
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
 * @file wire_frame.cpp
 * @brief Encodes and decodes the 12-byte frame header.
 * @author Mustafa Garip
 * @see modules/extension/wire/README.md
 */

#include <SushiRuntime/wire/wire_frame.hpp>

#include <cstring>
#include <string>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime::Distributed::Wire
{
    namespace
    {
        /** @brief Holds the offset of the payload length inside a header. */
        constexpr std::size_t K_PAYLOAD_LENGTH_OFFSET = 8;

        /** @brief Returns the flag bits a frame of the given type may carry. */
        constexpr std::uint8_t allowed_flags(MessageType type) noexcept
        {
            return type == MessageType::BufferChunk ? K_FRAME_FLAG_LAST_CHUNK : std::uint8_t{0};
        }
    } // namespace

    void append_frame_header(const FrameHeader& header, std::vector<std::byte>& out)
    {
        FieldWriter writer(out);
        writer.put_bytes({K_FRAME_MAGIC.data(), K_FRAME_MAGIC.size()});
        writer.put_u16(header.version);
        writer.put_enum(header.type);
        writer.put_u8(header.flags);
        writer.put_u32(header.payload_length);
    }

    FrameStatus decode_frame_header(SushiRuntime::span<const std::byte> bytes,
                                    FrameHeader& out) noexcept
    {
        if (bytes.size() < K_FRAME_HEADER_BYTES)
            return FrameStatus::Truncated;
        if (std::memcmp(bytes.data(), K_FRAME_MAGIC.data(), K_FRAME_MAGIC.size()) != 0)
            return FrameStatus::BadMagic;

        FieldReader reader(bytes.subspan(K_FRAME_MAGIC.size(),
                                         K_FRAME_HEADER_BYTES - K_FRAME_MAGIC.size()));
        const std::uint16_t version = reader.read_u16();
        const std::uint8_t type = reader.read_u8();
        const std::uint8_t flags = reader.read_u8();
        const std::uint32_t payload_length = reader.read_u32();

        if (version != K_PROTOCOL_VERSION)
        {
            out.version = version;
            return FrameStatus::VersionMismatch;
        }
        if (!is_message_type(type))
            return FrameStatus::UnknownType;
        if ((flags & ~allowed_flags(static_cast<MessageType>(type))) != 0)
            return FrameStatus::ReservedFlags;
        if (payload_length > K_MAX_FRAME_PAYLOAD)
            return FrameStatus::PayloadTooLong;

        out = FrameHeader{version, static_cast<MessageType>(type), flags, payload_length};
        return FrameStatus::Ok;
    }

    Result<void> check_protocol_version(std::uint16_t announced)
    {
        if (announced == K_PROTOCOL_VERSION)
            return {};
        return make_error(Error{
            ErrorCode::ProtocolMismatch,
            "the peer speaks protocol v" + std::to_string(announced) + " and this build v" +
                std::to_string(K_PROTOCOL_VERSION) + "; build both ends from one commit"});
    }

    void patch_payload_length(std::vector<std::byte>& frame, std::size_t header_offset,
                              std::uint32_t payload_length) noexcept
    {
        std::byte* field = frame.data() + header_offset + K_PAYLOAD_LENGTH_OFFSET;
        for (std::size_t index = 0; index < sizeof(payload_length); ++index)
            field[index] = static_cast<std::byte>((payload_length >> (8 * index)) & 0xFFu);
    }
} // namespace SushiRuntime::Distributed::Wire
