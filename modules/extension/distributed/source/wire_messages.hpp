/**************************************************************************/
/* wire_messages.hpp                                                      */
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
 * @file wire_messages.hpp
 * @brief Declares the sixteen messages of protocol version 2, each with encode and decode.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>

#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "wire_codec.hpp"

namespace SushiRuntime::Distributed::Wire
{
    /** @brief Holds the size of a handshake nonce in bytes. */
    inline constexpr std::size_t K_NONCE_BYTES = 32;

    /** @brief Holds the size of a handshake proof in bytes. */
    inline constexpr std::size_t K_PROOF_BYTES = 32;

    /** @brief Holds the encoded size of a BufferHandle: id, version, size. */
    inline constexpr std::size_t K_BUFFER_HANDLE_BYTES = 20;

    /**
     * @brief Names why a handshake is refused.
     */
    enum class RejectReason : std::uint8_t
    {
        Version = 1,    /**< The two sides carry different protocol versions. */
        Authentication, /**< The proof did not verify. */
        Malformed,      /**< A handshake frame did not decode. */
        Busy,           /**< The receiver takes no more peers. */
    };

    /**
     * @brief Names why a peer leaves a session.
     */
    enum class GoodbyeReason : std::uint8_t
    {
        Shutdown = 1, /**< The sender's process is stopping. */
        Liveness,     /**< The sender heard nothing from the receiver for too long. */
        Detach,       /**< The master detached its cluster. */
    };

    /**
     * @brief Names how a dispatched task ended on the worker.
     */
    enum class TaskResultStatus : std::uint8_t
    {
        Succeeded = 1,          /**< The kernel ran and every output chunk was sent. */
        KernelFailed,           /**< The kernel threw. */
        OutputAllocationFailed, /**< The worker could not allocate an output buffer. */
        InputUnavailable,       /**< An input buffer did not arrive. */
        UnknownOperation,       /**< The worker has no kernel for the operation. */
        ShuttingDown,           /**< The worker stopped before the task ran. */
    };

    /**
     * @brief Names what the bytes of a buffer chunk are for.
     */
    enum class ChunkPurpose : std::uint8_t
    {
        PullReply = 1, /**< The master answers a BufferRequest. */
        TaskOutput,    /**< The worker returns bytes a task wrote. */
    };

    /**
     * @brief Names why a buffer transfer stops.
     */
    enum class AbortReason : std::uint8_t
    {
        Stale = 1, /**< The buffer moved past the requested version. */
        Gone,      /**< The buffer is not shared any more. */
    };

    /**
     * @brief Opens the handshake: the worker's nonce and what it is.
     *
     * A decoded message's strings are views into the payload.
     */
    struct HelloMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Hello;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, HelloMessage& out);

        /** @brief Holds the worker's random nonce. */
        std::array<std::byte, K_NONCE_BYTES> worker_nonce{};

        /** @brief Holds the worker's build identifier, at most 255 bytes. */
        std::string_view build_id;

        /** @brief Holds the worker's display name, at most 255 bytes. */
        std::string_view node_name;
    };

    /**
     * @brief Answers a HELLO: the master's nonce, its proof and the session.
     */
    struct ChallengeMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Challenge;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, ChallengeMessage& out);

        /** @brief Holds the master's random nonce. */
        std::array<std::byte, K_NONCE_BYTES> master_nonce{};

        /** @brief Holds the master's proof that it knows the secret. */
        std::array<std::byte, K_PROOF_BYTES> master_proof{};

        /** @brief Holds the random identifier of the master's session. */
        std::uint64_t session_id = 0;
    };

    /**
     * @brief Answers a CHALLENGE: the worker's proof and what it can run.
     */
    struct ProofMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Proof;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out, reusing the capacity of its list. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, ProofMessage& out);

        /** @brief Holds the worker's proof that it knows the secret. */
        std::array<std::byte, K_PROOF_BYTES> worker_proof{};

        /** @brief Holds the operations the worker has a kernel for. */
        std::vector<Internal::Graph::OpID> ops;

        /** @brief Holds the number of tasks the worker queues at most. */
        std::uint32_t max_jobs = 0;

        /** @brief Holds the worker's buffer cache budget in bytes. */
        std::uint64_t cache_budget = 0;
    };

    /**
     * @brief Closes the handshake: the worker's id and the session's timings.
     */
    struct WelcomeMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Welcome;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, WelcomeMessage& out);

        /** @brief Holds the id the master gave the worker. */
        std::uint32_t worker_id = 0;

        /** @brief Holds the silence, in milliseconds, after which a peer counts as lost. */
        std::uint32_t liveness_timeout_ms = 0;

        /** @brief Holds the interval between the master's pings in milliseconds. */
        std::uint32_t ping_interval_ms = 0;

        /** @brief Holds the number of data bytes one buffer chunk carries at most. */
        std::uint32_t chunk_bytes = 0;
    };

    /**
     * @brief Refuses a handshake in words.
     *
     * The layout is frozen for every protocol version. A decoded message's text is a view
     * into the payload.
     */
    struct RejectMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Reject;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, RejectMessage& out);

        /** @brief Holds the cause. */
        RejectReason reason = RejectReason::Version;

        /** @brief Holds the protocol version of the side that refuses. */
        std::uint16_t sender_version = 0;

        /** @brief Holds a sentence for the receiver's log, at most 255 bytes. */
        std::string_view text;
    };

    /**
     * @brief Announces that the sender leaves the session.
     *
     * The layout is frozen for every protocol version. A decoded message's text is a view
     * into the payload.
     */
    struct GoodbyeMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Goodbye;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, GoodbyeMessage& out);

        /** @brief Holds the cause. */
        GoodbyeReason reason = GoodbyeReason::Shutdown;

        /** @brief Holds a sentence for the receiver's log, at most 255 bytes. */
        std::string_view text;
    };

    /**
     * @brief Tells a worker to run one kernel on the named buffers.
     */
    struct TaskDispatchMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::TaskDispatch;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out, reusing the capacity of its lists. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, TaskDispatchMessage& out);

        /** @brief Holds the master's id for the task. */
        std::uint64_t task_id = 0;

        /** @brief Holds the metadata; op_id selects the kernel and name stays off the wire. */
        Internal::Graph::TaskMetadata metadata;

        /** @brief Holds the buffers the kernel reads. */
        std::vector<BufferHandle> inputs;

        /** @brief Holds the buffers the kernel writes, at the versions its output must carry. */
        std::vector<BufferHandle> outputs;
    };

    /**
     * @brief Acknowledges that a dispatch arrived.
     */
    struct TaskAcceptedMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::TaskAccepted;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, TaskAcceptedMessage& out);

        /** @brief Holds the id of the accepted task. */
        std::uint64_t task_id = 0;
    };

    /**
     * @brief Reports how a task ended, after its output bytes travelled as chunks.
     */
    struct TaskResultMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::TaskResult;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out, reusing the capacity of its list. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, TaskResultMessage& out);

        /** @brief Holds the id of the task. */
        std::uint64_t task_id = 0;

        /** @brief Holds how the task ended. */
        TaskResultStatus status = TaskResultStatus::Succeeded;

        /** @brief Holds the buffers the task wrote. */
        std::vector<BufferHandle> outputs;
    };

    /**
     * @brief Asks the master for the bytes of one buffer version.
     */
    struct BufferRequestMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::BufferRequest;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, BufferRequestMessage& out);

        /** @brief Holds the buffer's identifier. */
        GlobalBufferID id;

        /** @brief Holds the version the worker needs. */
        std::uint32_t version = 0;
    };

    /**
     * @brief Carries one piece of a buffer's bytes.
     *
     * The data runs from the end of the fixed fields to the end of the payload. A decoded
     * message's data is a view into the payload.
     */
    struct BufferChunkMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::BufferChunk;

        /** @brief Holds the encoded size of the fields in front of the data. */
        static constexpr std::size_t K_PREFIX_BYTES = 37;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, BufferChunkMessage& out);

        /** @brief Holds what the bytes are for. */
        ChunkPurpose purpose = ChunkPurpose::PullReply;

        /** @brief Holds the task the bytes belong to; 0 for a pull reply. */
        std::uint64_t task_id = 0;

        /** @brief Holds the buffer, its version and its whole size. */
        BufferHandle handle;

        /** @brief Holds the position of the data inside the buffer. */
        std::uint64_t offset = 0;

        /** @brief Holds the bytes of this chunk. */
        SushiRuntime::span<const std::byte> data;
    };

    /**
     * @brief Stops the transfer of one buffer version.
     */
    struct BufferAbortMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::BufferAbort;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, BufferAbortMessage& out);

        /** @brief Holds the buffer's identifier. */
        GlobalBufferID id;

        /** @brief Holds the version whose transfer stops. */
        std::uint32_t version = 0;

        /** @brief Holds the task the transfer belonged to; 0 for a pull reply. */
        std::uint64_t task_id = 0;

        /** @brief Holds the cause. */
        AbortReason reason = AbortReason::Stale;
    };

    /**
     * @brief Tells a worker to drop a buffer from its cache.
     */
    struct BufferReleaseMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::BufferRelease;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, BufferReleaseMessage& out);

        /** @brief Holds the buffer's identifier. */
        GlobalBufferID id;
    };

    /**
     * @brief Probes a worker's round-trip time.
     */
    struct PingMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Ping;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, PingMessage& out);

        /** @brief Holds a value the worker echoes; only the master reads meaning into it. */
        std::uint64_t token = 0;
    };

    /**
     * @brief Answers a PING.
     */
    struct PongMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Pong;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, PongMessage& out);

        /** @brief Holds the token of the PING being answered. */
        std::uint64_t token = 0;

        /** @brief Holds the worker's free device memory in bytes; 0 when unknown. */
        std::uint64_t free_mem = 0;
    };

    /**
     * @brief Reports a worker's load between pings.
     */
    struct HeartbeatMessage
    {
        /** @brief Holds the type byte of this message. */
        static constexpr MessageType K_TYPE = MessageType::Heartbeat;

        /** @brief Appends the payload to out. */
        [[nodiscard]] SUSHIRUNTIME_API CodecStatus encode(std::vector<std::byte>& out) const;

        /** @brief Reads a payload into out. */
        [[nodiscard]] SUSHIRUNTIME_API static DecodeResult
        decode(SushiRuntime::span<const std::byte> payload, HeartbeatMessage& out);

        /** @brief Holds the number of tasks waiting on the worker. */
        std::uint32_t queue_depth = 0;

        /** @brief Holds the smallest task id the worker still holds. */
        std::uint64_t oldest_held_task_id = 0;

        /** @brief Holds the bytes the worker's buffer cache uses. */
        std::uint64_t cache_bytes = 0;
    };
} // namespace SushiRuntime::Distributed::Wire
