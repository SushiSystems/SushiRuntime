/**************************************************************************/
/* test_wire_format.cpp                                                   */
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
 * @file test_wire_format.cpp
 * @brief Tests the version 3 wire format against bytes written out by hand.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "message_router.hpp"
#include "wire_codec.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed::Wire;

namespace
{
    /** @brief Names a run of wire bytes. */
    using Bytes = std::vector<std::byte>;

    /** @brief Names a function that decodes a payload as one message type. */
    using DecodeFunction = DecodeResult (*)(SushiRuntime::span<const std::byte>);

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

    /** @brief Returns the first count bytes in an allocation of exactly that size. */
    Bytes prefix_of(const Bytes& bytes, std::size_t count)
    {
        return Bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(count));
    }

    /** @brief Returns 32 bytes that count up from first. */
    std::array<std::byte, 32> counting_from(unsigned first)
    {
        std::array<std::byte, 32> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::byte>(first + index);
        return bytes;
    }

    /** @brief Decodes a payload as Message and returns only the outcome. */
    template <typename Message>
    DecodeResult decode_as(SushiRuntime::span<const std::byte> payload)
    {
        Message message;
        return Message::decode(payload, message);
    }

    /**
     * @brief Checks that a message encodes to the golden bytes and that they decode.
     * @param golden Must outlive the returned message, whose views point into it.
     */
    template <typename Message>
    Message expect_golden(const Message& message, const Bytes& golden)
    {
        Bytes encoded;
        EXPECT_EQ(message.encode(encoded), CodecStatus::Ok);
        EXPECT_EQ(encoded, golden);

        Message decoded;
        const DecodeResult result = Message::decode(view_of(golden), decoded);
        EXPECT_EQ(result.status, CodecStatus::Ok);
        return decoded;
    }

    /** @brief Returns the HELLO payload: nonce 00..1F, "b1", "node". */
    Bytes golden_hello()
    {
        return bytes_of({
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
            0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,
            0x1C, 0x1D, 0x1E, 0x1F,
            0x02, 0x00, 0x62, 0x31,
            0x04, 0x00, 0x6E, 0x6F, 0x64, 0x65,
        });
    }

    /** @brief Returns the CHALLENGE payload: nonce 20..3F, proof 40..5F, a session id. */
    Bytes golden_challenge()
    {
        return bytes_of({
            0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D,
            0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
            0x3C, 0x3D, 0x3E, 0x3F,
            0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
            0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B,
            0x5C, 0x5D, 0x5E, 0x5F,
            0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        });
    }

    /** @brief Returns the PROOF payload: proof 60..7F, two operations, 1024 jobs, 4 GiB. */
    Bytes golden_proof()
    {
        return bytes_of({
            0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
            0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B,
            0x7C, 0x7D, 0x7E, 0x7F,
            0x02, 0x00, 0x00, 0x00,
            0x44, 0x33, 0x22, 0x11,
            0xDD, 0xCC, 0xBB, 0xAA,
            0x00, 0x04, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Returns the WELCOME payload: worker 7, 5000 ms, 1000 ms, 256 KiB. */
    Bytes golden_welcome()
    {
        return bytes_of({
            0x07, 0x00, 0x00, 0x00,
            0x88, 0x13, 0x00, 0x00,
            0xE8, 0x03, 0x00, 0x00,
            0x00, 0x00, 0x04, 0x00,
        });
    }

    /** @brief Returns the REJECT payload: version, sender version 3, "v3". */
    Bytes golden_reject()
    {
        return bytes_of({
            0x01,
            0x03, 0x00,
            0x02, 0x00, 0x76, 0x33,
        });
    }

    /** @brief Returns the GOODBYE payload: liveness, "bye". */
    Bytes golden_goodbye()
    {
        return bytes_of({
            0x02,
            0x03, 0x00, 0x62, 0x79, 0x65,
        });
    }

    /** @brief Returns the TASK_DISPATCH payload: task 42, two parameters, three handles. */
    Bytes golden_task_dispatch()
    {
        return bytes_of({
            0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x0D, 0xF0, 0xFE, 0xCA,
            0x04, 0x00, 0x61, 0x78, 0x70, 0x79,
            0x02,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40,
            0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00,
            0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00,
            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Returns the TASK_ACCEPTED payload: task 42. */
    Bytes golden_task_accepted()
    {
        return bytes_of({ 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    }

    /** @brief Returns the TASK_RESULT payload: task 42, succeeded, one output at version 2. */
    Bytes golden_task_result()
    {
        return bytes_of({
            0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01,
            0x01, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Returns the BUFFER_REQUEST payload: buffer 5 at version 9. */
    Bytes golden_buffer_request()
    {
        return bytes_of({
            0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x09, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Returns the BUFFER_CHUNK payload: task output, task 42, offset 64, four bytes. */
    Bytes golden_buffer_chunk()
    {
        return bytes_of({
            0x02,
            0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00,
            0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xDE, 0xAD, 0xBE, 0xEF,
        });
    }

    /** @brief Returns the BUFFER_ABORT payload: buffer 5, version 9, task 42, gone. */
    Bytes golden_buffer_abort()
    {
        return bytes_of({
            0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x09, 0x00, 0x00, 0x00,
            0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02,
        });
    }

    /** @brief Returns the BUFFER_RELEASE payload: buffer 5. */
    Bytes golden_buffer_release()
    {
        return bytes_of({ 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    }

    /** @brief Returns the PING payload: token 0x1122334455667788. */
    Bytes golden_ping()
    {
        return bytes_of({ 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 });
    }

    /** @brief Returns the PONG payload: the same token, 2 GiB free. */
    Bytes golden_pong()
    {
        return bytes_of({
            0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
            0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Returns the HEARTBEAT payload: depth 3, oldest task 41, 1 MiB cached. */
    Bytes golden_heartbeat()
    {
        return bytes_of({
            0x03, 0x00, 0x00, 0x00,
            0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
        });
    }

    /** @brief Holds one golden payload with its type and its decoder. */
    struct GoldenCase
    {
        /** @brief Holds the type byte the payload travels under. */
        MessageType type;

        /** @brief Holds the payload bytes. */
        Bytes payload;

        /** @brief Holds the decoder of the payload's message. */
        DecodeFunction decode;
    };

    /** @brief Returns the golden payload of every message type. */
    std::vector<GoldenCase> golden_cases()
    {
        return {
            { MessageType::Hello, golden_hello(), &decode_as<HelloMessage> },
            { MessageType::Challenge, golden_challenge(), &decode_as<ChallengeMessage> },
            { MessageType::Proof, golden_proof(), &decode_as<ProofMessage> },
            { MessageType::Welcome, golden_welcome(), &decode_as<WelcomeMessage> },
            { MessageType::Reject, golden_reject(), &decode_as<RejectMessage> },
            { MessageType::Goodbye, golden_goodbye(), &decode_as<GoodbyeMessage> },
            { MessageType::TaskDispatch, golden_task_dispatch(),
              &decode_as<TaskDispatchMessage> },
            { MessageType::TaskAccepted, golden_task_accepted(),
              &decode_as<TaskAcceptedMessage> },
            { MessageType::TaskResult, golden_task_result(), &decode_as<TaskResultMessage> },
            { MessageType::BufferRequest, golden_buffer_request(),
              &decode_as<BufferRequestMessage> },
            { MessageType::BufferChunk, golden_buffer_chunk(), &decode_as<BufferChunkMessage> },
            { MessageType::BufferAbort, golden_buffer_abort(), &decode_as<BufferAbortMessage> },
            { MessageType::BufferRelease, golden_buffer_release(),
              &decode_as<BufferReleaseMessage> },
            { MessageType::Ping, golden_ping(), &decode_as<PingMessage> },
            { MessageType::Pong, golden_pong(), &decode_as<PongMessage> },
            { MessageType::Heartbeat, golden_heartbeat(), &decode_as<HeartbeatMessage> },
        };
    }

    /** @brief Decodes a payload after one of its bytes is replaced. */
    template <typename Message>
    DecodeResult decode_with_byte(Bytes payload, std::size_t offset, unsigned value)
    {
        payload[offset] = static_cast<std::byte>(value);
        return decode_as<Message>(view_of(payload));
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
TEST(Unit_WireFormat, APeerThatAnnouncesVersionTwoIsRefusedWithProtocolMismatch)
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
TEST(Unit_WireFormat, FrameHeaderGolden)
{
    const Bytes golden = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x07, 0x00, 0x0C, 0x0B,
                                    0x0A, 0x00 });
    Bytes encoded;
    append_frame_header(FrameHeader{ 2, MessageType::TaskDispatch, 0, 0x000A0B0C }, encoded);
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
TEST(Unit_WireFormat, MagicReadsAsOversizedV1Length)
{
    Bytes frame;
    append_frame_header(FrameHeader{}, frame);

    std::uint32_t length = 0;
    for (std::size_t index = 0; index < 4; ++index)
        length |= static_cast<std::uint32_t>(frame[index]) << (8 * index);

    EXPECT_EQ(length, 0x50445253u);
    EXPECT_GT(length, 256u * 1024u * 1024u);
}

/** @brief Tests the HELLO bytes. */
TEST(Unit_WireFormat, GoldenBytes_Hello)
{
    HelloMessage message;
    message.worker_nonce = counting_from(0x00);
    message.build_id = "b1";
    message.node_name = "node";

    const Bytes golden = golden_hello();
    const HelloMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.worker_nonce, message.worker_nonce);
    EXPECT_EQ(decoded.build_id, "b1");
    EXPECT_EQ(decoded.node_name, "node");
}

/** @brief Tests the CHALLENGE bytes. */
TEST(Unit_WireFormat, GoldenBytes_Challenge)
{
    ChallengeMessage message;
    message.master_nonce = counting_from(0x20);
    message.master_proof = counting_from(0x40);
    message.session_id = 0x0102030405060708ull;

    const Bytes golden = golden_challenge();
    const ChallengeMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.master_nonce, message.master_nonce);
    EXPECT_EQ(decoded.master_proof, message.master_proof);
    EXPECT_EQ(decoded.session_id, 0x0102030405060708ull);
}

/** @brief Tests the PROOF bytes. */
TEST(Unit_WireFormat, GoldenBytes_Proof)
{
    ProofMessage message;
    message.worker_proof = counting_from(0x60);
    message.ops = { OperationID(0x11223344u), OperationID(0xAABBCCDDu) };
    message.max_jobs = 1024;
    message.cache_budget = 0x0000000100000000ull;

    const Bytes golden = golden_proof();
    const ProofMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.worker_proof, message.worker_proof);
    ASSERT_EQ(decoded.ops.size(), 2u);
    EXPECT_EQ(decoded.ops[0].value, 0x11223344u);
    EXPECT_EQ(decoded.ops[1].value, 0xAABBCCDDu);
    EXPECT_EQ(decoded.max_jobs, 1024u);
    EXPECT_EQ(decoded.cache_budget, 0x0000000100000000ull);
}

/** @brief Tests the WELCOME bytes. */
TEST(Unit_WireFormat, GoldenBytes_Welcome)
{
    WelcomeMessage message;
    message.worker_id = 7;
    message.liveness_timeout_ms = 5000;
    message.ping_interval_ms = 1000;
    message.chunk_bytes = 256u * 1024u;

    const Bytes golden = golden_welcome();
    const WelcomeMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.worker_id, 7u);
    EXPECT_EQ(decoded.liveness_timeout_ms, 5000u);
    EXPECT_EQ(decoded.ping_interval_ms, 1000u);
    EXPECT_EQ(decoded.chunk_bytes, 256u * 1024u);
}

/** @brief Tests the REJECT bytes, whose layout is frozen across versions. */
TEST(Unit_WireFormat, GoldenBytes_Reject)
{
    RejectMessage message;
    message.reason = RejectReason::Version;
    message.sender_version = 3;
    message.text = "v3";

    const Bytes golden = golden_reject();
    const RejectMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.reason, RejectReason::Version);
    EXPECT_EQ(decoded.sender_version, 3u);
    EXPECT_EQ(decoded.text, "v3");
}

/** @brief Tests the GOODBYE bytes, whose layout is frozen across versions. */
TEST(Unit_WireFormat, GoldenBytes_Goodbye)
{
    GoodbyeMessage message;
    message.reason = GoodbyeReason::Liveness;
    message.text = "bye";

    const Bytes golden = golden_goodbye();
    const GoodbyeMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.reason, GoodbyeReason::Liveness);
    EXPECT_EQ(decoded.text, "bye");
}

/** @brief Tests the TASK_DISPATCH bytes; the six zero parameters stay off the wire. */
TEST(Unit_WireFormat, GoldenBytes_TaskDispatch)
{
    TaskDispatchMessage message;
    message.task_id = 42;
    message.operation = OperationID(0xCAFEF00Du);
    message.name = "axpy";
    message.parameter_count = 2;
    message.parameters[0] = 0x4004000000000000ull;
    message.parameters[1] = 9;
    message.inputs = { BufferHandle{ GlobalBufferID{ 1 }, 3, 256 },
                       BufferHandle{ GlobalBufferID{ 2 }, 1, 512 } };
    message.outputs = { BufferHandle{ GlobalBufferID{ 3 }, 1, 128 } };

    const Bytes golden = golden_task_dispatch();
    const TaskDispatchMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.task_id, 42u);
    EXPECT_EQ(decoded.operation.value, 0xCAFEF00Du);
    EXPECT_EQ(decoded.name, "axpy");
    EXPECT_EQ(decoded.parameter_count, 2u);
    EXPECT_EQ(decoded.parameters[0], 0x4004000000000000ull);
    EXPECT_EQ(decoded.parameters[1], 9u);
    EXPECT_EQ(decoded.parameters[2], 0u);
    ASSERT_EQ(decoded.inputs.size(), 2u);
    EXPECT_EQ(decoded.inputs[0].id.value, 1u);
    EXPECT_EQ(decoded.inputs[0].version, 3u);
    EXPECT_EQ(decoded.inputs[0].size_bytes, 256u);
    EXPECT_EQ(decoded.inputs[1].id.value, 2u);
    EXPECT_EQ(decoded.inputs[1].version, 1u);
    EXPECT_EQ(decoded.inputs[1].size_bytes, 512u);
    ASSERT_EQ(decoded.outputs.size(), 1u);
    EXPECT_EQ(decoded.outputs[0].id.value, 3u);
    EXPECT_EQ(decoded.outputs[0].version, 1u);
    EXPECT_EQ(decoded.outputs[0].size_bytes, 128u);
}

/** @brief Tests the TASK_ACCEPTED bytes. */
TEST(Unit_WireFormat, GoldenBytes_TaskAccepted)
{
    TaskAcceptedMessage message;
    message.task_id = 42;

    const Bytes golden = golden_task_accepted();
    const TaskAcceptedMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.task_id, 42u);
}

/** @brief Tests the TASK_RESULT bytes. */
TEST(Unit_WireFormat, GoldenBytes_TaskResult)
{
    TaskResultMessage message;
    message.task_id = 42;
    message.status = TaskResultStatus::Succeeded;
    message.outputs = { BufferHandle{ GlobalBufferID{ 3 }, 2, 128 } };

    const Bytes golden = golden_task_result();
    const TaskResultMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.task_id, 42u);
    EXPECT_EQ(decoded.status, TaskResultStatus::Succeeded);
    ASSERT_EQ(decoded.outputs.size(), 1u);
    EXPECT_EQ(decoded.outputs[0].id.value, 3u);
    EXPECT_EQ(decoded.outputs[0].version, 2u);
    EXPECT_EQ(decoded.outputs[0].size_bytes, 128u);
}

/** @brief Tests the BUFFER_REQUEST bytes. */
TEST(Unit_WireFormat, GoldenBytes_BufferRequest)
{
    BufferRequestMessage message;
    message.id = GlobalBufferID{ 5 };
    message.version = 9;

    const Bytes golden = golden_buffer_request();
    const BufferRequestMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.id.value, 5u);
    EXPECT_EQ(decoded.version, 9u);
}

/** @brief Tests the BUFFER_CHUNK bytes; the data runs to the end of the payload. */
TEST(Unit_WireFormat, GoldenBytes_BufferChunk)
{
    const Bytes data = bytes_of({ 0xDE, 0xAD, 0xBE, 0xEF });
    BufferChunkMessage message;
    message.purpose = ChunkPurpose::TaskOutput;
    message.task_id = 42;
    message.handle = BufferHandle{ GlobalBufferID{ 3 }, 2, 128 };
    message.offset = 64;
    message.data = view_of(data);

    const Bytes golden = golden_buffer_chunk();
    const BufferChunkMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.purpose, ChunkPurpose::TaskOutput);
    EXPECT_EQ(decoded.task_id, 42u);
    EXPECT_EQ(decoded.handle.id.value, 3u);
    EXPECT_EQ(decoded.handle.version, 2u);
    EXPECT_EQ(decoded.handle.size_bytes, 128u);
    EXPECT_EQ(decoded.offset, 64u);
    ASSERT_EQ(decoded.data.size(), data.size());
    EXPECT_EQ(std::memcmp(decoded.data.data(), data.data(), data.size()), 0);
    EXPECT_EQ(golden.size(), BufferChunkMessage::K_PREFIX_BYTES + data.size());
}

/** @brief Tests the BUFFER_ABORT bytes. */
TEST(Unit_WireFormat, GoldenBytes_BufferAbort)
{
    BufferAbortMessage message;
    message.id = GlobalBufferID{ 5 };
    message.version = 9;
    message.task_id = 42;
    message.reason = AbortReason::Gone;

    const Bytes golden = golden_buffer_abort();
    const BufferAbortMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.id.value, 5u);
    EXPECT_EQ(decoded.version, 9u);
    EXPECT_EQ(decoded.task_id, 42u);
    EXPECT_EQ(decoded.reason, AbortReason::Gone);
}

/** @brief Tests the BUFFER_RELEASE bytes. */
TEST(Unit_WireFormat, GoldenBytes_BufferRelease)
{
    BufferReleaseMessage message;
    message.id = GlobalBufferID{ 5 };

    const Bytes golden = golden_buffer_release();
    const BufferReleaseMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.id.value, 5u);
}

/** @brief Tests the PING bytes. */
TEST(Unit_WireFormat, GoldenBytes_Ping)
{
    PingMessage message;
    message.token = 0x1122334455667788ull;

    const Bytes golden = golden_ping();
    const PingMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.token, 0x1122334455667788ull);
}

/** @brief Tests the PONG bytes. */
TEST(Unit_WireFormat, GoldenBytes_Pong)
{
    PongMessage message;
    message.token = 0x1122334455667788ull;
    message.free_mem = 0x80000000ull;

    const Bytes golden = golden_pong();
    const PongMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.token, 0x1122334455667788ull);
    EXPECT_EQ(decoded.free_mem, 0x80000000ull);
}

/** @brief Tests the HEARTBEAT bytes. */
TEST(Unit_WireFormat, GoldenBytes_Heartbeat)
{
    HeartbeatMessage message;
    message.queue_depth = 3;
    message.oldest_held_task_id = 41;
    message.cache_bytes = 0x100000ull;

    const Bytes golden = golden_heartbeat();
    const HeartbeatMessage decoded = expect_golden(message, golden);
    EXPECT_EQ(decoded.queue_depth, 3u);
    EXPECT_EQ(decoded.oldest_held_task_id, 41u);
    EXPECT_EQ(decoded.cache_bytes, 0x100000ull);
}

/** @brief Tests that every strict prefix of every golden frame is refused. */
TEST(Unit_WireFormat, EveryTruncationFailsCleanly)
{
    for (const GoldenCase& golden : golden_cases())
    {
        SCOPED_TRACE(message_type_name(golden.type));
        ASSERT_TRUE(golden.decode(view_of(golden.payload)).ok());

        for (std::size_t length = 0; length < golden.payload.size(); ++length)
        {
            const Bytes prefix = prefix_of(golden.payload, length);
            const DecodeResult result = golden.decode(view_of(prefix));
            // A chunk cut inside its data is a shorter chunk; the frame length catches it.
            const bool cut_inside_chunk_data = golden.type == MessageType::BufferChunk
                                               && length >= BufferChunkMessage::K_PREFIX_BYTES;
            EXPECT_EQ(result.ok(), cut_inside_chunk_data) << "payload prefix " << length;
            EXPECT_LE(result.offset, length);
        }

        Bytes frame;
        const auto payload_length = static_cast<std::uint32_t>(golden.payload.size());
        append_frame_header(FrameHeader{ K_PROTOCOL_VERSION, golden.type, 0, payload_length },
                            frame);
        frame.insert(frame.end(), golden.payload.begin(), golden.payload.end());

        for (std::size_t length = 0; length < frame.size(); ++length)
        {
            const Bytes prefix = prefix_of(frame, length);
            FrameHeader header;
            const FrameStatus status = decode_frame_header(view_of(prefix), header);
            if (length < K_FRAME_HEADER_BYTES)
            {
                EXPECT_EQ(status, FrameStatus::Truncated) << "frame prefix " << length;
                continue;
            }
            ASSERT_EQ(status, FrameStatus::Ok) << "frame prefix " << length;
            EXPECT_LT(length - K_FRAME_HEADER_BYTES, header.payload_length);
        }
    }
}

/** @brief Tests the range checks on bools, enumerations, bounded bytes and header fields. */
TEST(Unit_WireFormat, RejectsOutOfRangeEnumAndBool)
{
    const Bytes bools = bytes_of({ 0x00, 0x01, 0x02 });
    FieldReader reader(view_of(bools));
    EXPECT_FALSE(reader.read_bool());
    EXPECT_TRUE(reader.read_bool());
    EXPECT_TRUE(reader.ok());
    EXPECT_FALSE(reader.read_bool());
    const DecodeResult bool_result = reader.finish();
    EXPECT_EQ(bool_result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(bool_result.offset, 2u);

    DecodeResult result = decode_with_byte<RejectMessage>(golden_reject(), 0, 0);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 0u);
    result = decode_with_byte<RejectMessage>(golden_reject(), 0, 5);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    result = decode_with_byte<GoodbyeMessage>(golden_goodbye(), 0, 4);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    result = decode_with_byte<TaskDispatchMessage>(golden_task_dispatch(), 12, 5);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 12u);
    result = decode_with_byte<TaskDispatchMessage>(golden_task_dispatch(), 21, 9);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 21u);
    result = decode_with_byte<TaskResultMessage>(golden_task_result(), 8, 0);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 8u);
    result = decode_with_byte<TaskResultMessage>(golden_task_result(), 8, 7);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    result = decode_with_byte<BufferChunkMessage>(golden_buffer_chunk(), 0, 3);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    result = decode_with_byte<BufferAbortMessage>(golden_buffer_abort(), 20, 3);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 20u);

    FrameHeader header;
    EXPECT_EQ(header_status_with_byte(0, 0x54, header), FrameStatus::BadMagic);
    EXPECT_EQ(header_status_with_byte(6, 0x00, header), FrameStatus::UnknownType);
    EXPECT_EQ(header_status_with_byte(6, 0x11, header), FrameStatus::UnknownType);
    EXPECT_EQ(header_status_with_byte(7, 0x02, header), FrameStatus::ReservedFlags);
    EXPECT_EQ(header_status_with_byte(7, 0x01, header), FrameStatus::ReservedFlags);
    EXPECT_EQ(header_status_with_byte(4, 0x02, header), FrameStatus::VersionMismatch);
    EXPECT_EQ(header.version, 2u);

    const Bytes largest = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x0B, 0x00, 0x00,
                                     0x00, 0x10, 0x00 });
    EXPECT_EQ(decode_frame_header(view_of(largest), header), FrameStatus::Ok);
    const Bytes too_long = bytes_of({ 0x53, 0x52, 0x44, 0x50, 0x03, 0x00, 0x0B, 0x00, 0x01,
                                      0x00, 0x10, 0x00 });
    EXPECT_EQ(decode_frame_header(view_of(too_long), header), FrameStatus::PayloadTooLong);
}

/** @brief Tests that a list count the payload cannot hold fails before any reserve. */
TEST(Unit_WireFormat, ListCountBoundedByRemainingBytes)
{
    Bytes proof = prefix_of(golden_proof(), K_PROOF_BYTES);
    const Bytes huge_count = bytes_of({ 0xFF, 0xFF, 0xFF, 0xFF });
    const Bytes twelve_bytes(12, std::byte{ 0 });
    proof.insert(proof.end(), huge_count.begin(), huge_count.end());
    proof.insert(proof.end(), twelve_bytes.begin(), twelve_bytes.end());

    ProofMessage proof_message;
    DecodeResult result = ProofMessage::decode(view_of(proof), proof_message);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, K_PROOF_BYTES);
    EXPECT_EQ(proof_message.ops.capacity(), 0u);

    // Twelve bytes hold three operations, so a count of four is one too many.
    proof[K_PROOF_BYTES] = std::byte{ 0x04 };
    proof[K_PROOF_BYTES + 1] = std::byte{ 0x00 };
    proof[K_PROOF_BYTES + 2] = std::byte{ 0x00 };
    proof[K_PROOF_BYTES + 3] = std::byte{ 0x00 };
    result = ProofMessage::decode(view_of(proof), proof_message);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(proof_message.ops.capacity(), 0u);

    // 0x0CCCCCCD handles of 20 bytes each come to 2^32 + 4 bytes, past a 32-bit product.
    const std::size_t inputs_offset = 38;
    Bytes dispatch = prefix_of(golden_task_dispatch(), inputs_offset);
    const Bytes wrapping_count = bytes_of({ 0xCD, 0xCC, 0xCC, 0x0C });
    const Bytes two_handles(2 * K_BUFFER_HANDLE_BYTES, std::byte{ 0 });
    dispatch.insert(dispatch.end(), wrapping_count.begin(), wrapping_count.end());
    dispatch.insert(dispatch.end(), two_handles.begin(), two_handles.end());

    TaskDispatchMessage dispatch_message;
    result = TaskDispatchMessage::decode(view_of(dispatch), dispatch_message);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, inputs_offset);
    EXPECT_EQ(dispatch_message.inputs.capacity(), 0u);
}

/** @brief Tests that a payload with bytes after its last field is refused. */
TEST(Unit_WireFormat, RejectsTrailingBytes)
{
    Bytes ping = golden_ping();
    ping.push_back(std::byte{ 0x00 });

    PingMessage message;
    const DecodeResult result = PingMessage::decode(view_of(ping), message);
    EXPECT_EQ(result.status, CodecStatus::TrailingBytes);
    EXPECT_EQ(result.offset, 8u);
}

/** @brief Tests that a string outside 255 bytes fails on both sides and leaves no frame. */
TEST(Unit_WireFormat, StringLengthBoundedAt255)
{
    const std::string long_text(256, 'a');
    GoodbyeMessage message;
    message.text = long_text;

    Bytes frame = bytes_of({ 0xAA });
    EXPECT_EQ(append_frame(message, 0, frame), CodecStatus::TooLong);
    EXPECT_EQ(frame, bytes_of({ 0xAA }));

    Bytes payload = bytes_of({ 0x01, 0x00, 0x01 });
    payload.insert(payload.end(), 256, std::byte{ 0x61 });
    GoodbyeMessage decoded;
    const DecodeResult result = GoodbyeMessage::decode(view_of(payload), decoded);
    EXPECT_EQ(result.status, CodecStatus::OutOfRange);
    EXPECT_EQ(result.offset, 1u);
}

/** @brief Tests that the router calls the handler of the type and reports the rest. */
TEST(Unit_WireFormat, RouterDeliversByType)
{
    MessageRouter<int&> router;
    router.set_handler(MessageType::Ping, [](int& calls) { ++calls; });

    int calls = 0;
    EXPECT_TRUE(router.has_handler(MessageType::Ping));
    EXPECT_FALSE(router.has_handler(MessageType::Pong));
    EXPECT_EQ(router.route(MessageType::Ping, calls), RouteStatus::Delivered);
    EXPECT_EQ(router.route(MessageType::Pong, calls), RouteStatus::Unhandled);
    EXPECT_EQ(router.route(static_cast<MessageType>(0), calls), RouteStatus::Unhandled);
    EXPECT_EQ(calls, 1);
}
