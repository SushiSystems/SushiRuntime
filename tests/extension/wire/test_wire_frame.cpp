/**************************************************************************/
/* test_wire_frame.cpp                                                    */
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
 * @file test_wire_frame.cpp
 * @brief Tests the frame header: its golden bytes, the version check and the magic.
 * @see tests/extension/wire/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/transport/message_type.hpp>

#include <SushiRuntime/wire/wire_frame.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed::Wire;

namespace
{
    /** @brief Names a run of wire bytes. */
    using Bytes = std::vector<std::byte>;

    /** @brief Builds a byte vector from literal values. */
    Bytes bytes_of(std::initializer_list<unsigned> values)
    {
        Bytes bytes;
        bytes.reserve(values.size());
        for (unsigned value : values)
            bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    }

    /** @brief Returns a read-only view of a byte vector. */
    SushiRuntime::span<const std::byte> view_of(const Bytes& bytes)
    {
        return { bytes.data(), bytes.size() };
    }

    /** @brief Decodes a frame header after one of its bytes is replaced. */
    FrameStatus header_status_with_byte(std::size_t offset, unsigned value, FrameHeader& out)
    {
        Bytes header = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x0E, 0x00, 0x08, 0x00,
                                  0x00, 0x00 });
        header[offset] = static_cast<std::byte>(value);
        return decode_frame_header(view_of(header), out);
    }
}

/** @brief Tests that a peer whose header announces version 2 is refused with ProtocolMismatch. */
TEST(Medium_WireFormat, APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch)
{
    FrameHeader header;
    ASSERT_EQ(header_status_with_byte(4, 0x02, header), FrameStatus::VersionMismatch);
    ASSERT_EQ(header.version, 2u);

    const Result<void> refused = check_protocol_version(header.version);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::ProtocolMismatch);
    EXPECT_NE(refused.error().message.find("protocol v2"), std::string::npos)
        << refused.error().message;
    EXPECT_NE(refused.error().message.find("this build v3"), std::string::npos)
        << refused.error().message;

    EXPECT_TRUE(check_protocol_version(K_PROTOCOL_VERSION).has_value());
    static_assert(K_PROTOCOL_VERSION == 3, "the golden headers of this file carry version 3");
}

/** @brief Tests the 12-byte header layout, the last-chunk flag and a whole PING frame. */
TEST(Medium_WireFormat, FrameHeaderGolden)
{
    const Bytes golden = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x07, 0x00, 0x0C, 0x0B,
                                    0x0A, 0x00 });
    Bytes encoded;
    append_frame_header(FrameHeader{ K_PROTOCOL_VERSION, MessageType::TaskDispatch, 0, 0x000A0B0C }, encoded);
    EXPECT_EQ(encoded, golden);

    FrameHeader header;
    ASSERT_EQ(decode_frame_header(view_of(golden), header), FrameStatus::Ok);
    EXPECT_EQ(header.version, 3u);
    EXPECT_EQ(header.type, MessageType::TaskDispatch);
    EXPECT_EQ(header.flags, 0u);
    EXPECT_EQ(header.payload_length, 0x000A0B0Cu);

    const Bytes last_chunk = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x0B, 0x01, 0x25,
                                        0x00, 0x00, 0x00 });
    ASSERT_EQ(decode_frame_header(view_of(last_chunk), header), FrameStatus::Ok);
    EXPECT_EQ(header.type, MessageType::BufferChunk);
    EXPECT_EQ(header.flags, K_FRAME_FLAG_LAST_CHUNK);
    EXPECT_EQ(header.payload_length, 37u);

    const Bytes ping_frame = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x0E, 0x00, 0x08,
                                        0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33,
                                        0x22, 0x11 });
    PingMessage ping;
    ping.token = 0x1122334455667788ull;
    Bytes frame;
    ASSERT_EQ(append_frame(ping, 0, frame), CodecStatus::Ok);
    EXPECT_EQ(frame, ping_frame);
}

/** @brief Tests that a version 1 reader sees the magic as a length above its 256 MiB cap. */
TEST(Medium_WireFormat, MagicReadsAsOversizedV1Length)
{
    Bytes frame;
    append_frame_header(FrameHeader{}, frame);

    std::uint32_t length = 0;
    for (std::size_t index = 0; index < 4; ++index)
        length |= static_cast<std::uint32_t>(frame[index]) << (8 * index);

    EXPECT_EQ(length, 0x50445253u);
    EXPECT_GT(length, 256u * 1024u * 1024u);
}
