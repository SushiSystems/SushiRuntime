/**************************************************************************/
/* master_link.hpp                                                        */
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
 * @file master_link.hpp
 * @brief Declares MasterLink, the worker's side of the handshake, heartbeat and master watch.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/i_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>

#include "handshake.hpp"
#include "wire_messages.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds what the link tells the master about this worker, and its timing.
         */
        struct MasterLinkOptions
        {
            /** @brief Holds the name sent in HELLO, at most 255 bytes. */
            std::string name;

            /** @brief Holds the build identifier sent in HELLO, at most 255 bytes. */
            std::string build_id;

            /** @brief Holds the operations sent in PROOF: KernelRegistry::known_ops(). */
            std::vector<Graph::OpID> ops;

            /** @brief Holds the job queue's bound, sent in PROOF. */
            std::uint32_t max_jobs = 1024;

            /** @brief Holds the cache's byte budget, sent in PROOF. */
            std::uint64_t cache_budget = 0;

            /** @brief Holds the time between two HEARTBEAT frames. */
            std::chrono::milliseconds heartbeat_interval{1000};

            /** @brief Returns the HEARTBEAT to send; called on the heartbeat thread, unlocked. */
            std::function<Wire::HeartbeatMessage()> heartbeat_probe;

            /** @brief Returns free device memory for PONG; called on the reader thread. */
            std::function<std::uint64_t()> free_memory_probe;
        };

        /**
         * @brief Names what a call changed, so the agent can act on its other bricks.
         */
        enum class LinkChange : std::uint8_t
        {
            None,                 /**< Nothing the agent must act on. */
            Registered,           /**< WELCOME arrived from the session known before. */
            RegisteredNewSession, /**< WELCOME from another session: clear cache and ids. */
            Disconnected,         /**< The master is gone: discard the queued jobs. */
            Ended,                /**< The link reached a final WorkerEnd; see end(). */
        };

        /**
         * @brief Registers with the master on each connection and watches that it stays there.
         *
         * The reader thread calls the on_ members; the link's own thread sends HEARTBEAT,
         * restarts a handshake that got no WELCOME within one interval, and closes the peer
         * when no frame came for the liveness timeout WELCOME carried. The link owns one
         * mutex and releases it before it calls the transport or a probe.
         */
        class SUSHIRUNTIME_API MasterLink final
        {
            public:
                /**
                 * @brief Builds a link; no thread runs until start().
                 * @param secret The cluster's key, possibly empty; the bytes outlive the link.
                 * @param nonces Called once per handshake; a test passes a fixed source.
                 */
                MasterLink(ITransport& transport, SushiRuntime::span<const std::uint8_t> secret,
                           MasterLinkOptions options, NonceSource nonces);

                /** @brief Calls stop(). */
                ~MasterLink();

                MasterLink(const MasterLink&) = delete;
                MasterLink& operator=(const MasterLink&) = delete;
                MasterLink(MasterLink&&) = delete;
                MasterLink& operator=(MasterLink&&) = delete;

                /** @brief Starts the heartbeat thread; called once, before the transport starts. */
                void start();

                /** @brief Joins the heartbeat thread; a Running end becomes Stopped. */
                void stop() noexcept;

                /** @brief Sends HELLO with a fresh nonce, or closes the peer of an ended link. */
                void on_connected(PeerId peer);

                /**
                 * @brief Verifies the master's proof and answers with PROOF.
                 * @return Ended, with RejectedAuthentication, when the proof is wrong; the
                 *         peer is then closed.
                 */
                LinkChange on_challenge(PeerId peer, const Wire::ChallengeMessage& challenge);

                /** @brief Records the worker id, the liveness timeout and the chunk size. */
                LinkChange on_welcome(PeerId peer, const Wire::WelcomeMessage& welcome);

                /**
                 * @brief Records why the master refused.
                 * @return Ended for Authentication and Version; None for Busy and Malformed,
                 *         after which the next connection tries again.
                 */
                LinkChange on_reject(PeerId peer, const Wire::RejectMessage& reject);

                /** @brief Answers a PING with its token and the free memory probe's value. */
                void on_ping(PeerId peer, const Wire::PingMessage& ping);

                /** @brief Notes that the master is leaving; the loss follows from the transport. */
                void on_goodbye(PeerId peer, const Wire::GoodbyeMessage& goodbye);

                /** @brief Refreshes the master's liveness; called for every frame. */
                void note_frame(PeerId peer) noexcept;

                /**
                 * @brief Forgets the connection.
                 * @return Ended, with ProtocolMismatch, after BadMagic or VersionMismatch;
                 *         Disconnected when it was registered; None otherwise.
                 */
                LinkChange on_lost(PeerId peer, const PeerLoss& loss);

                /**
                 * @brief Ends the link: MasterUnreachable when it never registered, MasterLost
                 *        when it had.
                 */
                LinkChange on_connect_abandoned();

                /** @brief Queues GOODBYE(shutdown) to the master when registered. */
                void say_goodbye() noexcept;

                /** @brief Returns the peer the link is registered with, or nothing. */
                [[nodiscard]] std::optional<PeerId> master() const;

                /** @brief Returns the chunk size of the current session; 0 before WELCOME. */
                [[nodiscard]] std::uint32_t chunk_bytes() const noexcept;

                /** @brief Returns Running, or the final end. */
                [[nodiscard]] WorkerEnd end() const noexcept;

                /** @brief Blocks until the end is not Running, or the timeout; returns end(). */
                [[nodiscard]] WorkerEnd wait_for_end(std::chrono::milliseconds timeout);

                /** @brief Returns the registrations after the first. */
                [[nodiscard]] std::uint64_t reconnects() const noexcept;

            private:
                /** @brief Runs the heartbeat and the two watches until stop(). */
                void run() noexcept;

                /// @brief Guards the session state; released before any call out.
                mutable std::mutex mutex_;

                /// @brief Wakes wait_for_end() and ends the heartbeat sleep.
                std::condition_variable changed_;

                /// @brief Carries the link's frames and closes a silent master.
                ITransport& transport_;

                /// @brief The cluster's key, borrowed.
                SushiRuntime::span<const std::uint8_t> secret_;

                /// @brief Identity, capacity, timing and probes.
                MasterLinkOptions options_;

                /// @brief Makes the nonce of each handshake.
                NonceSource nonces_;

                /// @brief The heartbeat thread.
                std::thread thread_;

                /// @brief Set by stop().
                bool stopping_ = false;

                /// @brief Running, or the final end.
                std::atomic<WorkerEnd> end_{WorkerEnd::Running};

                /// @brief Chunk size of the current session.
                std::atomic<std::uint32_t> chunk_bytes_{0};

                /// @brief Registrations after the first.
                std::atomic<std::uint64_t> reconnects_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
