/**************************************************************************/
/* worker_directory.hpp                                                   */
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
 * @file worker_directory.hpp
 * @brief Declares WorkerDirectory, the master's record of each peer's session and load.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "handshake.hpp"
#include "sha256.hpp"
#include "wire_messages.hpp"

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Names what the directory decided about one handshake frame.
         */
        enum class HandshakeStep : std::uint8_t
        {
            Accepted,             /**< The frame was the one expected; send the reply. */
            UnknownPeer,          /**< The peer is not in the directory; drop the frame. */
            OutOfOrder,           /**< Not the frame this peer owes; drop it. */
            AuthenticationFailed, /**< The worker's proof is wrong; send REJECT and close. */
            AlreadyRegistered,    /**< HELLO from a registered worker; send WELCOME again. */
        };

        /**
         * @brief Carries the outcome of accept_hello().
         */
        struct HelloReply
        {
            /** @brief What the directory decided. */
            HandshakeStep step = HandshakeStep::UnknownPeer;

            /** @brief Holds the proof CHALLENGE carries; meaningful when step is Accepted. */
            Sha256Digest master_proof{};
        };

        /**
         * @brief Describes a peer the directory has just forgotten.
         */
        struct RemovedPeer
        {
            /** @brief Tells whether the peer was in the directory at all. */
            bool known = false;

            /** @brief Tells whether it had finished the handshake and counted as a worker. */
            bool registered = false;

            /** @brief Holds the name from HELLO; empty before HELLO. */
            std::string name;

            /** @brief Holds the endpoint the transport reported. */
            std::string endpoint;
        };

        /**
         * @brief Describes who a registered worker is, for the line that logs its registration.
         */
        struct WorkerIdentity
        {
            /** @brief Holds the name from HELLO. */
            std::string name;

            /** @brief Holds the endpoint the transport reported. */
            std::string endpoint;

            /** @brief Holds the build identifier from HELLO. */
            std::string build_id;
        };

        /**
         * @brief Keeps one session per connected peer: handshake state, kernels, liveness, load.
         *
         * A worker's id is the value of its PeerID. The directory owns one mutex, held only
         * inside its members; the single call it makes while holding it is
         * OffloadPolicy::pick_worker(). It calls no other brick, no transport and no sink.
         * alive_count() reads an atomic. Any thread may call any member.
         */
        class SUSHIRUNTIME_API WorkerDirectory final
        {
            public:
                /**
                 * @brief Builds an open, empty directory.
                 * @param secret The cluster's key, possibly empty; the bytes outlive this.
                 * @param policy Not null; called under the directory's mutex.
                 */
                WorkerDirectory(SushiRuntime::span<const std::uint8_t> secret,
                                std::shared_ptr<OffloadPolicy> policy);

                /** @brief Destroys the directory. */
                ~WorkerDirectory();

                WorkerDirectory(const WorkerDirectory&) = delete;
                WorkerDirectory& operator=(const WorkerDirectory&) = delete;
                WorkerDirectory(WorkerDirectory&&) = delete;
                WorkerDirectory& operator=(WorkerDirectory&&) = delete;

                /** @brief Adds a peer that owes HELLO; its handshake time starts at @p now. */
                void add_peer(PeerID peer, std::string endpoint,
                              std::chrono::steady_clock::time_point now);

                /**
                 * @brief Forgets a peer after on_peer_lost(); a worker stops counting as alive
                 *        and its kernels stop counting toward supports().
                 */
                RemovedPeer remove_peer(PeerID peer);

                /**
                 * @brief Returns whether a frame of this type is to be handled for this peer.
                 *
                 * HELLO and GOODBYE pass for any known peer, PROOF for a peer that owes it,
                 * every other type only for a registered worker. The rest is dropped unread.
                 */
                [[nodiscard]] bool admits(PeerID peer, MessageType type) const;

                /**
                 * @brief Records a HELLO and computes the master's proof for CHALLENGE.
                 *
                 * A second HELLO from a peer that is not registered restarts its handshake
                 * with the new nonces. From a registered worker, which lost its WELCOME, it
                 * changes nothing and answers AlreadyRegistered.
                 * @param master_nonce Fresh for this call; the caller sends the same bytes.
                 */
                [[nodiscard]] HelloReply accept_hello(PeerID peer, const Wire::HelloMessage& hello,
                                                      const HandshakeNonce& master_nonce);

                /**
                 * @brief Verifies a PROOF and, when it holds, registers the peer as a worker.
                 *
                 * On Accepted the worker is alive, its kernels count toward supports() and
                 * wait_for_workers() wakes; the caller sends WELCOME with the peer's value.
                 * A closed directory registers nobody and answers OutOfOrder.
                 */
                [[nodiscard]] HandshakeStep accept_proof(PeerID peer,
                                                         const Wire::ProofMessage& proof,
                                                         std::chrono::steady_clock::time_point now);

                /** @brief Refreshes a peer's liveness; called for every frame it sends. */
                void note_frame(PeerID peer, std::chrono::steady_clock::time_point now);

                /** @brief Records the round trip and the free memory a PONG reports. */
                void note_pong(PeerID peer, std::uint32_t rtt_micros, std::uint64_t free_mem);

                /** @brief Records the queue depth and the cache size a HEARTBEAT reports. */
                void note_heartbeat(PeerID peer, std::uint32_t queue_depth,
                                    std::uint64_t cache_bytes);

                /**
                 * @brief Asks the policy for a worker and counts the task against it.
                 *
                 * The policy sees the alive workers whose inflight count is below the max_jobs
                 * they sent in PROOF. On a pick that worker's inflight count rises by one.
                 * @return Nothing when the directory is closed, no worker has room or the
                 *         policy keeps the task local.
                 */
                [[nodiscard]] std::optional<PeerID> pick(const Internal::Graph::Node& node,
                                                         std::uint64_t estimated_bytes);

                /**
                 * @brief Lowers a worker's inflight count by one, never below 0.
                 *
                 * Called once per PendingTask a removal returned, and for nothing else.
                 */
                void release_inflight(PeerID worker) noexcept;

                /**
                 * @brief Lists the workers silent for longer than @p liveness and marks each
                 *        not alive, so pick() skips it; remove_peer() follows the close.
                 * @param out Receives the peers, appended; each is listed once.
                 */
                void take_silent(std::chrono::steady_clock::time_point now,
                                 std::chrono::milliseconds liveness, std::vector<PeerID>& out);

                /**
                 * @brief Lists the peers that connected more than @p limit ago and are still
                 *        not registered.
                 * @param out Receives the peers, appended; each is listed once.
                 */
                void take_handshake_overdue(std::chrono::steady_clock::time_point now,
                                            std::chrono::milliseconds limit,
                                            std::vector<PeerID>& out);

                /** @brief Appends every alive worker, for a PING or a BUFFER_RELEASE to each. */
                void list_workers(std::vector<PeerID>& out) const;

                /** @brief Returns whether an alive worker sent this operation in PROOF. */
                [[nodiscard]] bool supports(OperationID op) const;

                /** @brief Returns the number of alive workers; reads an atomic. */
                [[nodiscard]] std::size_t alive_count() const noexcept;

                /**
                 * @brief Blocks on a condition, without polling, until @p count workers are
                 *        alive, the timeout passes or close() is called.
                 * @return True when the count was reached; false once close() was called.
                 */
                [[nodiscard]] bool wait_for_workers(std::size_t count,
                                                    std::chrono::milliseconds timeout);

                /** @brief Returns who a registered worker is, or nothing for any other peer. */
                [[nodiscard]] std::optional<WorkerIdentity> identity(PeerID worker) const;

                /** @brief Appends one snapshot per alive worker. */
                void snapshot(std::chrono::steady_clock::time_point now,
                              std::vector<WorkerSnapshot>& out) const;

                /**
                 * @brief Marks every worker not alive, as a lost peer is, and wakes every waiter.
                 *
                 * From here on pick() returns nothing, supports() is false for every operation
                 * and no peer registers. The sessions stay until remove_peer() forgets them.
                 */
                void close() noexcept;

            private:
                /** @brief Names how far the handshake of a peer has come. */
                enum class Phase : std::uint8_t
                {
                    AwaitsHello, /**< Connected; owes HELLO. */
                    AwaitsProof, /**< Sent HELLO; owes PROOF. */
                    Registered,  /**< Proved the secret; a worker. */
                };

                /** @brief Holds the session of one peer. */
                struct Session
                {
                    /** @brief How far the handshake has come. */
                    Phase phase = Phase::AwaitsHello;

                    /** @brief Whether the peer is a worker that pick() may offer. */
                    bool alive = false;

                    /** @brief Whether take_handshake_overdue() listed the peer. */
                    bool handshake_overdue_listed = false;

                    /** @brief The name from HELLO. */
                    std::string name;

                    /** @brief The endpoint the transport reported. */
                    std::string endpoint;

                    /** @brief The build identifier from HELLO. */
                    std::string build_id;

                    /** @brief The nonce of the last HELLO. */
                    HandshakeNonce worker_nonce{};

                    /** @brief The nonce the master answered that HELLO with. */
                    HandshakeNonce master_nonce{};

                    /** @brief The operations from PROOF. */
                    std::vector<OperationID> operations;

                    /** @brief The task limit from PROOF. */
                    std::uint32_t max_jobs = 0;

                    /** @brief Tasks picked for the worker and not released. */
                    std::uint32_t inflight = 0;

                    /** @brief The round trip of the last PONG in microseconds. */
                    std::uint32_t rtt_micros = 0;

                    /** @brief The queue depth of the last HEARTBEAT. */
                    std::uint32_t queue_depth = 0;

                    /** @brief The free memory of the last PONG in bytes. */
                    std::uint64_t free_mem = 0;

                    /** @brief The cache size of the last HEARTBEAT in bytes. */
                    std::uint64_t cache_bytes = 0;

                    /** @brief The time add_peer() was given. */
                    std::chrono::steady_clock::time_point connected_at;

                    /** @brief The time of the last frame of the peer. */
                    std::chrono::steady_clock::time_point last_frame;
                };

                /** @brief Returns whether pick() may offer the worker of a session. */
                [[nodiscard]] static bool has_room(const Session& session) noexcept;

                /** @brief Marks a worker not alive and uncounts it; called under the mutex. */
                void retire(Session& session) noexcept;

                /// @brief Guards the sessions; held across OffloadPolicy::pick_worker() only.
                mutable std::mutex mutex_;

                /// @brief Wakes wait_for_workers() on a registration and on close().
                std::condition_variable registered_;

                /// @brief The cluster's key, borrowed.
                SushiRuntime::span<const std::uint8_t> secret_;

                /// @brief Chooses the worker in pick().
                std::shared_ptr<OffloadPolicy> policy_;

                /// @brief The number of alive workers, for alive_count().
                std::atomic<std::size_t> alive_count_{0};

                /// @brief Set by close().
                bool closed_ = false;

                /// @brief The sessions by the value of their PeerID, in rising order.
                std::map<std::uint32_t, Session> sessions_;

                /// @brief The number of alive workers per operation id value.
                std::unordered_map<std::uint32_t, std::size_t> workers_per_operation_;

                /// @brief The workers offered to the policy; reused by every pick().
                std::vector<WorkerLoad> candidates_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
