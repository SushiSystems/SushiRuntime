/**************************************************************************/
/* worker_wire.hpp                                                        */
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
 * @file worker_wire.hpp
 * @brief Declares the frames and builders a test of a worker plays the master with.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/link_security/sha256.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>
#include <SushiRuntime/transport/recording_transport_events.hpp>
#include <SushiRuntime/wire/wire_codec.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

namespace TestSupport::WorkerWire
{
    using namespace SushiRuntime;
    using namespace SushiRuntime::Distributed;

    /** @brief Holds the byte every key byte of a worker test holds. */
    inline constexpr std::uint8_t K_SECRET_BYTE = 0x42;

    /** @brief Returns a handle. */
    inline BufferHandle handle_of(std::uint64_t id, std::uint32_t version,
                                  std::uint64_t size_bytes)
    {
        BufferHandle handle;
        handle.id = GlobalBufferID{id};
        handle.version = version;
        handle.size_bytes = size_bytes;
        return handle;
    }

    /** @brief Returns @p count bytes that count up from @p first. */
    inline std::vector<std::byte> counting_bytes(std::size_t count, std::uint8_t first)
    {
        std::vector<std::byte> bytes(count);
        for (std::size_t index = 0; index < count; ++index)
            bytes[index] = static_cast<std::byte>(first + index);
        return bytes;
    }

    /** @brief Returns a pull-reply chunk that carries part of @p bytes. */
    inline Wire::BufferChunkMessage pull_chunk(const BufferHandle& handle, std::uint64_t offset,
                                               std::size_t length,
                                               const std::vector<std::byte>& bytes)
    {
        Wire::BufferChunkMessage chunk;
        chunk.purpose = Wire::ChunkPurpose::PullReply;
        chunk.handle = handle;
        chunk.offset = offset;
        chunk.data = span<const std::byte>(bytes.data() + offset, length);
        return chunk;
    }

    /** @brief Decodes the payload of a recorded frame; the frame outlives the message. */
    template <typename Message>
    Message decode(const RecordedFrame& frame)
    {
        Message message;
        const Wire::DecodeResult result = Message::decode(
            span<const std::byte>(frame.payload.data(), frame.payload.size()), message);
        EXPECT_TRUE(result.ok()) << "payload failed to decode at offset " << result.offset;
        return message;
    }

    /** @brief Returns a dispatch of one task. */
    inline Wire::TaskDispatchMessage dispatch_of(std::uint64_t task_id, OperationID op,
                                                 std::vector<BufferHandle> inputs,
                                                 std::vector<BufferHandle> outputs)
    {
        Wire::TaskDispatchMessage dispatch;
        dispatch.task_id = task_id;
        dispatch.operation = op;
        dispatch.inputs = std::move(inputs);
        dispatch.outputs = std::move(outputs);
        return dispatch;
    }

    /** @brief Returns a BUFFER_RELEASE of one buffer. */
    inline Wire::BufferReleaseMessage release_of(std::uint64_t id)
    {
        Wire::BufferReleaseMessage release;
        release.id = GlobalBufferID{id};
        return release;
    }

    /** @brief Returns a nonce whose bytes all hold @p value. */
    inline HandshakeNonce nonce_of(std::uint8_t value)
    {
        HandshakeNonce nonce{};
        nonce.bytes.fill(value);
        return nonce;
    }

    /** @brief Returns the nonce the master of these tests answers with. */
    inline HandshakeNonce master_nonce()
    {
        return nonce_of(0x22);
    }

    /** @brief Returns 32 key bytes that all hold @p value. */
    inline std::vector<std::uint8_t> secret_of(std::uint8_t value)
    {
        return std::vector<std::uint8_t>(32, value);
    }

    /** @brief Returns the worker nonce a HELLO carries. */
    inline HandshakeNonce nonce_in(const Wire::HelloMessage& hello)
    {
        HandshakeNonce nonce{};
        std::memcpy(nonce.bytes.data(), hello.worker_nonce.data(), nonce.bytes.size());
        return nonce;
    }

    /** @brief Returns the CHALLENGE a master holding @p secret sends for a worker's nonce. */
    inline Wire::ChallengeMessage challenge_of(const std::vector<std::uint8_t>& secret,
                                               std::uint64_t session_id,
                                               const HandshakeNonce& worker_nonce)
    {
        const HandshakeNonce nonce = master_nonce();
        const Sha256Digest proof =
            compute_proof(HandshakeRole::Master,
                          span<const std::uint8_t>(secret.data(), secret.size()), worker_nonce,
                          nonce);

        Wire::ChallengeMessage challenge;
        std::memcpy(challenge.master_nonce.data(), nonce.bytes.data(),
                    challenge.master_nonce.size());
        std::memcpy(challenge.master_proof.data(), proof.data(), challenge.master_proof.size());
        challenge.session_id = session_id;
        return challenge;
    }

    /** @brief Returns a WELCOME with a liveness timeout and a chunk size. */
    inline Wire::WelcomeMessage welcome_of(PeerID peer, std::uint32_t liveness_timeout_ms,
                                           std::uint32_t chunk_bytes)
    {
        Wire::WelcomeMessage welcome;
        welcome.worker_id = peer.value;
        welcome.liveness_timeout_ms = liveness_timeout_ms;
        welcome.ping_interval_ms = 250;
        welcome.chunk_bytes = chunk_bytes;
        return welcome;
    }
} // namespace TestSupport::WorkerWire
