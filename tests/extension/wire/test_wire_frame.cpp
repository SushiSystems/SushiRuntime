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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
        Bytes header = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x04, 0x00, 0x0E, 0x00, 0x08, 0x00,
                                  0x00, 0x00 });
        header[offset] = static_cast<std::byte>(value);
        return decode_frame_header(view_of(header), out);
    }
    /** @brief Returns the first four bytes of a frame as a little-endian number. */
    std::uint32_t little_endian_u32(const Bytes& frame)
    {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index)
            value |= static_cast<std::uint32_t>(frame[index]) << (8 * index);
        return value;
    }
}


namespace
{
    /** @brief Names the version a header announces for a test report. */
    std::string name_of_version(const ::testing::TestParamInfo<unsigned>& info)
    {
        return "Version" + std::to_string(info.param);
    }
} // namespace

/** @brief Runs a test once per older protocol version a peer can announce. */
class Small_WireOlderPeer : public ::testing::TestWithParam<unsigned>
{
};

/** @brief Tests that a peer whose header announces an older version is refused with ProtocolMismatch. */
TEST_P(Small_WireOlderPeer, IsRefusedWithProtocolMismatch)
{
    FrameHeader header;
    ASSERT_EQ(header_status_with_byte(4, GetParam(), header), FrameStatus::VersionMismatch);
    ASSERT_EQ(header.version, GetParam());

    const Result<void> refused = check_protocol_version(header.version);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::ProtocolMismatch);
    EXPECT_NE(refused.error().message.find("protocol v" + std::to_string(GetParam())),
              std::string::npos)
        << refused.error().message;
    EXPECT_NE(refused.error().message.find("this build v" + std::to_string(K_PROTOCOL_VERSION)),
              std::string::npos)
        << refused.error().message;
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_WireOlderPeer, ::testing::Values(2u, 3u), name_of_version);

/** @brief Tests that the version of this build is accepted. */
TEST(Small_WireFormat, TheVersionOfThisBuildIsAccepted)
{
    EXPECT_TRUE(check_protocol_version(K_PROTOCOL_VERSION).has_value());
}

/** @brief Tests the 12-byte header layout in both directions. */
TEST(Small_WireFormat, FrameHeaderGolden)
{
    const Bytes golden = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x04, 0x00, 0x07, 0x00, 0x0C, 0x0B,
                                    0x0A, 0x00 });
    Bytes encoded;
    append_frame_header(FrameHeader{ K_PROTOCOL_VERSION, MessageType::TaskDispatch, 0, 0x000A0B0C }, encoded);
    EXPECT_EQ(encoded, golden);

    FrameHeader header;
    ASSERT_EQ(decode_frame_header(view_of(golden), header), FrameStatus::Ok);
    EXPECT_EQ(header.version, 4u);
    EXPECT_EQ(header.type, MessageType::TaskDispatch);
    EXPECT_EQ(header.flags, 0u);
    EXPECT_EQ(header.payload_length, 0x000A0B0Cu);
}

/** @brief Tests that the last-chunk flag of a header decodes. */
TEST(Small_WireFormat, TheLastChunkFlagDecodes)
{
    const Bytes last_chunk = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x04, 0x00, 0x0B, 0x01, 0x25,
                                        0x00, 0x00, 0x00 });
    FrameHeader header;

    ASSERT_EQ(decode_frame_header(view_of(last_chunk), header), FrameStatus::Ok);

    EXPECT_EQ(header.type, MessageType::BufferChunk);
    EXPECT_EQ(header.flags, K_FRAME_FLAG_LAST_CHUNK);
    EXPECT_EQ(header.payload_length, 37u);
}

/** @brief Tests the bytes of a whole PING frame. */
TEST(Small_WireFormat, PingFrameGolden)
{
    const Bytes ping_frame = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x04, 0x00, 0x0E, 0x00, 0x08,
                                        0x00, 0x00, 0x00, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33,
                                        0x22, 0x11 });
    PingMessage ping;
    ping.token = 0x1122334455667788ull;
    Bytes frame;

    ASSERT_EQ(append_frame(ping, 0, frame), CodecStatus::Ok);

    EXPECT_EQ(frame, ping_frame);
}

/** @brief Tests that the magic bytes, read as a little-endian number, are 0x50445253. */
TEST(Small_WireFormat, MagicReadsAsALittleEndianNumber)
{
    Bytes frame;
    append_frame_header(FrameHeader{}, frame);

    EXPECT_EQ(little_endian_u32(frame), 0x50445253u);
}
