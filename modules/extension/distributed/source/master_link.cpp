/**************************************************************************/
/* master_link.cpp                                                        */
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
 * @file master_link.cpp
 * @brief Implements MasterLink: the worker's handshake, its heartbeat and the master watch.
 * @author Mustafa Garip
 */

#include "master_link.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <utility>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns a nonce as the handshake functions take it. */
            HandshakeNonce to_handshake(const std::array<std::byte, Wire::K_NONCE_BYTES>& wire)
            {
                HandshakeNonce nonce{};
                std::memcpy(nonce.bytes.data(), wire.data(), nonce.bytes.size());
                return nonce;
            }

            /** @brief Returns 32 bytes of a nonce or a proof as the wire carries them. */
            std::array<std::byte, Wire::K_NONCE_BYTES> to_wire(
                const std::array<std::uint8_t, Wire::K_NONCE_BYTES>& bytes)
            {
                std::array<std::byte, Wire::K_NONCE_BYTES> wire{};
                std::memcpy(wire.data(), bytes.data(), wire.size());
                return wire;
            }

            /** @brief Returns the words a log line uses for a loss cause. */
            const char* loss_cause_name(PeerLossCause cause) noexcept
            {
                switch (cause)
                {
                    case PeerLossCause::ClosedByPeer:
                        return "closed by the master";
                    case PeerLossCause::ClosedLocally:
                        return "closed by this worker";
                    case PeerLossCause::IOFailed:
                        return "read or write failed";
                    case PeerLossCause::WriteStalled:
                        return "write stalled";
                    case PeerLossCause::BadMagic:
                        return "not a SushiRuntime frame";
                    case PeerLossCause::VersionMismatch:
                        return "protocol version mismatch";
                    case PeerLossCause::MalformedFrame:
                        return "malformed frame";
                    case PeerLossCause::HandlerFailed:
                        return "a frame handler failed";
                }
                return "unknown cause";
            }

            /** @brief Returns the words a log line uses for a goodbye reason. */
            const char* goodbye_reason_name(Wire::GoodbyeReason reason) noexcept
            {
                switch (reason)
                {
                    case Wire::GoodbyeReason::Shutdown:
                        return "shutdown";
                    case Wire::GoodbyeReason::Liveness:
                        return "liveness";
                    case Wire::GoodbyeReason::Detach:
                        return "detach";
                }
                return "unknown reason";
            }
        } // namespace

        bool MasterLink::is_handshaking() const noexcept
        {
            return phase_ == Phase::HelloSent || phase_ == Phase::ProofSent;
        }

        MasterLink::Clock::time_point MasterLink::last_frame() const noexcept
        {
            return Clock::time_point(
                Clock::duration(last_frame_ticks_.load(std::memory_order_relaxed)));
        }

        void MasterLink::stamp_frame() noexcept
        {
            last_frame_ticks_.store(Clock::now().time_since_epoch().count(),
                                    std::memory_order_relaxed);
        }

        void MasterLink::forget_connection() noexcept
        {
            peer_ = PeerID{};
            phase_ = Phase::Idle;
            sent_nonce_count_ = 0;
            hello_count_ = 0;
            proof_sent_ = false;
            watched_peer_.store(0, std::memory_order_relaxed);
        }

        bool MasterLink::finish(WorkerEnd end)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                WorkerEnd expected = WorkerEnd::Running;
                if (!end_.compare_exchange_strong(expected, end))
                    return false;
            }
            changed_.notify_all();
            return true;
        }

        void MasterLink::send_hello(PeerID peer)
        {
            const Result<HandshakeNonce> drawn = nonces_();
            if (!drawn)
            {
                SR_LOG_ERROR("cannot draw a handshake nonce for master (peer {}): {}; closing "
                             "the connection",
                             peer.value, drawn.error().message);
                transport_.close_peer(peer);
                return;
            }
            const HandshakeNonce nonce = *drawn;

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (peer_ != peer || !is_handshaking())
                    return;

                std::rotate(sent_nonces_.rbegin(), sent_nonces_.rbegin() + 1,
                            sent_nonces_.rend());
                sent_nonces_[0] = nonce;
                sent_nonce_count_ = std::min(sent_nonce_count_ + 1, K_NONCE_HISTORY);
                ++hello_count_;
                phase_ = Phase::HelloSent;
                hello_at_ = Clock::now();
            }

            Wire::HelloMessage hello;
            hello.worker_nonce = to_wire(nonce.bytes);
            hello.build_id = options_.build_id;
            hello.node_name = options_.name;
            const SendStatus status = send_message(transport_, peer, hello);
            if (status != SendStatus::Queued)
                SR_LOG_DEBUG("HELLO to master (peer {}) was not queued (status {})", peer.value,
                             static_cast<int>(status));
        }

        MasterLink::MasterLink(Transport& transport,
                               SushiRuntime::span<const std::uint8_t> secret,
                               MasterLinkOptions options, NonceSource nonces)
            : transport_(transport), secret_(secret), options_(std::move(options)),
              nonces_(std::move(nonces))
        {
            options_.heartbeat_interval =
                std::max(options_.heartbeat_interval, std::chrono::milliseconds(1));
        }

        MasterLink::~MasterLink()
        {
            stop();
        }

        void MasterLink::start()
        {
            thread_ = std::thread(&MasterLink::run, this);
        }

        void MasterLink::stop() noexcept
        {
            leaving_.store(true, std::memory_order_release);
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            changed_.notify_all();
            if (thread_.joinable())
                thread_.join();
            finish(WorkerEnd::Stopped);
        }

        void MasterLink::on_connected(PeerID peer)
        {
            if (end() != WorkerEnd::Running || leaving_.load(std::memory_order_acquire))
            {
                transport_.close_peer(peer);
                return;
            }

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                forget_connection();
                peer_ = peer;
                phase_ = Phase::HelloSent;
                hello_at_ = Clock::now();
                watched_peer_.store(peer.value, std::memory_order_relaxed);
                stamp_frame();
            }
            changed_.notify_all();
            send_hello(peer);
        }

        LinkChange MasterLink::on_challenge(PeerID peer, const Wire::ChallengeMessage& challenge)
        {
            Nonces nonces{};
            std::size_t nonce_count = 0;
            bool has_forgotten_nonce = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (end() != WorkerEnd::Running || peer_ != peer ||
                    !is_handshaking() || sent_nonce_count_ == 0)
                    return LinkChange::None;

                nonces = sent_nonces_;
                nonce_count = sent_nonce_count_;
                has_forgotten_nonce = hello_count_ > sent_nonce_count_;
            }

            const HandshakeNonce master_nonce = to_handshake(challenge.master_nonce);
            Sha256Digest master_proof{};
            std::memcpy(master_proof.data(), challenge.master_proof.data(), master_proof.size());

            std::size_t answered = nonce_count;
            for (std::size_t index = 0; index < nonce_count && answered == nonce_count; ++index)
            {
                if (verify_proof(HandshakeRole::Master, secret_, nonces[index], master_nonce,
                                 master_proof))
                    answered = index;
            }

            if (answered == nonce_count && has_forgotten_nonce)
            {
                // The proof may answer a HELLO whose nonce is forgotten, so it proves nothing.
                SR_LOG_DEBUG("CHALLENGE from master (peer {}) matches none of the last {} "
                             "HELLOs and earlier ones are forgotten; ignored, the next HELLO "
                             "runs the handshake",
                             peer.value, nonce_count);
                return LinkChange::None;
            }

            if (answered == nonce_count)
            {
                if (finish(WorkerEnd::RejectedAuthentication))
                    SR_LOG_ERROR("master (peer {}) did not prove the cluster secret; the secret "
                                 "files of the two machines differ; closed, not reconnecting",
                                 peer.value);
                transport_.close_peer(peer);
                return LinkChange::Ended;
            }

            if (answered != 0)
            {
                SR_LOG_DEBUG("CHALLENGE from master (peer {}) answers an earlier HELLO; ignored",
                             peer.value);
                return LinkChange::None;
            }

            Wire::ProofMessage proof;
            proof.worker_proof = to_wire(
                compute_proof(HandshakeRole::Worker, secret_, nonces[0], master_nonce));
            proof.ops = options_.ops;
            proof.max_jobs = options_.max_jobs;
            proof.cache_budget = options_.cache_budget;

            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (peer_ != peer || !is_handshaking() ||
                    sent_nonces_[0].bytes != nonces[0].bytes)
                    return LinkChange::None;

                phase_ = Phase::ProofSent;
                proof_sent_ = true;
                offered_session_ = challenge.session_id;
            }

            const SendStatus status = send_message(transport_, peer, proof);
            if (status != SendStatus::Queued)
                SR_LOG_DEBUG("PROOF to master (peer {}) was not queued (status {})", peer.value,
                             static_cast<int>(status));
            return LinkChange::None;
        }

        LinkChange MasterLink::on_welcome(PeerID peer, const Wire::WelcomeMessage& welcome)
        {
            bool new_session = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (end() != WorkerEnd::Running || peer_ != peer || !proof_sent_ ||
                    !is_handshaking())
                    return LinkChange::None;

                new_session = !ever_registered_ || session_ != offered_session_;
                registrations_.fetch_add(1, std::memory_order_relaxed);

                phase_ = Phase::Registered;
                session_ = offered_session_;
                ever_registered_ = true;
                liveness_ = std::chrono::milliseconds(welcome.liveness_timeout_ms);
                heartbeat_at_ = Clock::now() + options_.heartbeat_interval;
                stamp_frame();
                chunk_bytes_.store(welcome.chunk_bytes, std::memory_order_relaxed);
            }
            changed_.notify_all();

            SR_LOG_INFO("registered with master (peer {}) as worker {}: liveness {} ms, chunks "
                        "of {} bytes, {} session",
                        peer.value, welcome.worker_id, welcome.liveness_timeout_ms,
                        welcome.chunk_bytes, new_session ? "new" : "known");
            return new_session ? LinkChange::RegisteredNewSession : LinkChange::Registered;
        }

        LinkChange MasterLink::on_reject(PeerID peer, const Wire::RejectMessage& reject)
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (peer_ != peer)
                    return LinkChange::None;
            }

            const std::string text(reject.text);
            switch (reject.reason)
            {
                case Wire::RejectReason::Authentication:
                    if (finish(WorkerEnd::RejectedAuthentication))
                        SR_LOG_ERROR("master (peer {}) rejected this worker's secret: {}; the "
                                     "secret files of the two machines differ; not reconnecting",
                                     peer.value, text);
                    return LinkChange::Ended;
                case Wire::RejectReason::Version:
                    if (finish(WorkerEnd::ProtocolMismatch))
                        SR_LOG_ERROR("master (peer {}) speaks protocol v{}, this worker v{}; "
                                     "build both from one commit; not reconnecting",
                                     peer.value, reject.sender_version,
                                     Wire::K_PROTOCOL_VERSION);
                    return LinkChange::Ended;
                case Wire::RejectReason::Busy:
                    SR_LOG_WARN("master (peer {}) takes no more workers: {}; the next "
                                "connection tries again",
                                peer.value, text);
                    return LinkChange::None;
                case Wire::RejectReason::Malformed:
                    SR_LOG_WARN("master (peer {}) could not read this worker's handshake: {}; "
                                "the next connection tries again",
                                peer.value, text);
                    return LinkChange::None;
            }
            return LinkChange::None;
        }

        void MasterLink::on_ping(PeerID peer, const Wire::PingMessage& ping)
        {
            {
                // A PONG from a link that is not registered would keep a master waiting on it.
                const std::lock_guard<std::mutex> lock(mutex_);
                if (peer_ != peer || phase_ != Phase::Registered)
                    return;
            }

            Wire::PongMessage pong;
            pong.token = ping.token;
            if (options_.free_memory_probe)
                pong.free_mem = options_.free_memory_probe();

            const SendStatus status = send_message(transport_, peer, pong);
            if (status != SendStatus::Queued)
                SR_LOG_DEBUG("PONG to master (peer {}) was not queued (status {})", peer.value,
                             static_cast<int>(status));
        }

        void MasterLink::on_goodbye(PeerID peer, const Wire::GoodbyeMessage& goodbye)
        {
            SR_LOG_INFO("master (peer {}) is leaving ({}): {}", peer.value,
                        goodbye_reason_name(goodbye.reason), std::string(goodbye.text));
        }

        void MasterLink::note_frame(PeerID peer) noexcept
        {
            if (watched_peer_.load(std::memory_order_relaxed) == peer.value)
                stamp_frame();
        }

        LinkChange MasterLink::on_lost(PeerID peer, const PeerLoss& loss)
        {
            bool was_registered = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (peer_ != peer)
                    return LinkChange::None;

                was_registered = phase_ == Phase::Registered ||
                                 phase_ == Phase::Closing;
                forget_connection();
            }
            changed_.notify_all();

            if (leaving_.load(std::memory_order_acquire))
            {
                SR_LOG_DEBUG("connection to the master (peer {}) ended while this worker "
                             "leaves: {} {}",
                             peer.value, loss_cause_name(loss.cause), loss.text);
                return LinkChange::None;
            }

            if (loss.cause == PeerLossCause::VersionMismatch)
            {
                if (finish(WorkerEnd::ProtocolMismatch))
                    SR_LOG_ERROR("master (peer {}) speaks protocol v{}, this worker v{}; build "
                                 "both from one commit; not reconnecting",
                                 peer.value, loss.peer_version, Wire::K_PROTOCOL_VERSION);
                return LinkChange::Ended;
            }

            if (loss.cause == PeerLossCause::BadMagic)
            {
                if (finish(WorkerEnd::ProtocolMismatch))
                    SR_LOG_ERROR("peer {} is not a SushiRuntime master: {}; not reconnecting",
                                 peer.value, loss.text);
                return LinkChange::Ended;
            }

            if (was_registered)
            {
                SR_LOG_WARN("master (peer {}) lost: {}{}{}; queued jobs are dropped and the "
                            "transport decides whether to reconnect",
                            peer.value, loss_cause_name(loss.cause),
                            loss.text.empty() ? "" : " ", loss.text);
                return LinkChange::Disconnected;
            }

            if (end() == WorkerEnd::Running)
                SR_LOG_WARN("master (peer {}) connection ended during the handshake: {}{}{}; a "
                            "protocol or secret mismatch is likely",
                            peer.value, loss_cause_name(loss.cause),
                            loss.text.empty() ? "" : " ", loss.text);
            return LinkChange::None;
        }

        LinkChange MasterLink::on_connect_abandoned()
        {
            if (leaving_.load(std::memory_order_acquire))
            {
                SR_LOG_DEBUG("the transport stopped connecting to the master while this worker "
                             "leaves");
                return LinkChange::None;
            }

            bool ever_registered = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                ever_registered = ever_registered_;
            }

            const WorkerEnd end =
                ever_registered ? WorkerEnd::MasterLost : WorkerEnd::MasterUnreachable;
            if (finish(end))
                SR_LOG_ERROR("the transport stopped connecting to the master {}; this worker "
                             "serves no one from here on",
                             ever_registered ? "after losing it" : "before reaching it once");
            return LinkChange::Ended;
        }

        void MasterLink::say_goodbye() noexcept
        {
            leaving_.store(true, std::memory_order_release);
            changed_.notify_all();
            try
            {
                const std::optional<PeerID> peer = master();
                if (!peer)
                    return;

                Wire::GoodbyeMessage goodbye;
                goodbye.reason = Wire::GoodbyeReason::Shutdown;
                goodbye.text = "worker stopping";
                const SendStatus status = send_message(transport_, *peer, goodbye);
                if (status != SendStatus::Queued)
                    SR_LOG_DEBUG("GOODBYE to master (peer {}) was not queued (status {})",
                                 peer->value, static_cast<int>(status));
            }
            catch (const std::exception& error)
            {
                SR_LOG_WARN("GOODBYE to the master was not sent: {}; the master finds the "
                            "worker gone when the connection closes",
                            error.what());
            }
            catch (...)
            {
                SR_LOG_WARN("GOODBYE to the master was not sent: unknown exception; the master "
                            "finds the worker gone when the connection closes");
            }
        }

        std::optional<PeerID> MasterLink::master() const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (phase_ != Phase::Registered)
                return std::nullopt;
            return peer_;
        }

        std::uint32_t MasterLink::chunk_bytes() const noexcept
        {
            return chunk_bytes_.load(std::memory_order_relaxed);
        }

        WorkerEnd MasterLink::end() const noexcept
        {
            return end_.load(std::memory_order_acquire);
        }

        WorkerEnd MasterLink::wait_for_end(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait_for(lock, timeout, [this]
                              {
                                  return end() != WorkerEnd::Running;
                              });
            return end();
        }

        std::uint64_t MasterLink::registrations() const noexcept
        {
            return registrations_.load(std::memory_order_relaxed);
        }

        std::uint64_t MasterLink::reconnects() const noexcept
        {
            const std::uint64_t accepted = registrations();
            return accepted == 0 ? 0 : accepted - 1;
        }

        void MasterLink::run() noexcept
        {
            /** @brief Names what one pass of the thread does once the mutex is released. */
            enum class Action : std::uint8_t
            {
                Wait,          /**< Nothing is due. */
                StopTransport, /**< Stop the transport of a link that reached a final end. */
                Heartbeat,     /**< Send HEARTBEAT. */
                Restart,       /**< Send HELLO again with a fresh nonce. */
                CloseSilent,   /**< Close a master that sent nothing for the liveness timeout. */
            };

            std::unique_lock<std::mutex> lock(mutex_);
            while (!stopping_)
            {
                const Clock::time_point now = Clock::now();
                const PeerID peer = peer_;
                Action action = Action::Wait;
                std::optional<Clock::time_point> wake;
                std::chrono::milliseconds silence{0};
                std::chrono::milliseconds limit{0};

                const bool is_leaving = leaving_.load(std::memory_order_acquire);
                const bool running = end() == WorkerEnd::Running && !is_leaving;

                if (end() != WorkerEnd::Running && !is_leaving && !transport_stopped_)
                {
                    action = Action::StopTransport;
                    transport_stopped_ = true;
                }
                else if (running && phase_ == Phase::Registered)
                {
                    const Clock::time_point last = last_frame();
                    const bool watched = liveness_ > std::chrono::milliseconds::zero();
                    if (watched && now - last >= liveness_)
                    {
                        action = Action::CloseSilent;
                        phase_ = Phase::Closing;
                        silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last);
                        limit = liveness_;
                    }
                    else if (now >= heartbeat_at_)
                    {
                        action = Action::Heartbeat;
                        heartbeat_at_ = now + options_.heartbeat_interval;
                    }
                    else
                    {
                        wake = watched ? std::min(heartbeat_at_, last + liveness_)
                                       : heartbeat_at_;
                    }
                }
                else if (running && is_handshaking())
                {
                    if (now - hello_at_ >= options_.heartbeat_interval)
                    {
                        action = Action::Restart;
                        hello_at_ = now;
                    }
                    else
                    {
                        wake = hello_at_ + options_.heartbeat_interval;
                    }
                }

                if (action == Action::Wait)
                {
                    if (wake)
                        changed_.wait_until(lock, *wake);
                    else
                        changed_.wait(lock);
                    continue;
                }

                lock.unlock();
                try
                {
                    if (action == Action::StopTransport)
                    {
                        SR_LOG_DEBUG("the link to the master ended; stopping its transport, "
                                     "which connects no more");
                        transport_.stop();
                    }
                    else if (action == Action::CloseSilent)
                    {
                        SR_LOG_WARN("no frame from master (peer {}) for {} ms (limit {} ms); "
                                    "closing the connection so the transport can reconnect",
                                    peer.value, silence.count(), limit.count());
                        transport_.close_peer(peer);
                    }
                    else if (action == Action::Restart)
                    {
                        SR_LOG_DEBUG("no WELCOME from master (peer {}) within {} ms; sending "
                                     "HELLO again with a fresh nonce",
                                     peer.value, options_.heartbeat_interval.count());
                        send_hello(peer);
                    }
                    else
                    {
                        const Wire::HeartbeatMessage heartbeat =
                            options_.heartbeat_probe ? options_.heartbeat_probe()
                                                     : Wire::HeartbeatMessage{};
                        const SendStatus status = send_message(transport_, peer, heartbeat);
                        if (status != SendStatus::Queued)
                            SR_LOG_DEBUG("HEARTBEAT to master (peer {}) was not queued (status "
                                         "{})",
                                         peer.value, static_cast<int>(status));
                    }
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR("the heartbeat thread failed a step toward master (peer {}): "
                                 "{}; the thread keeps running",
                                 peer.value, error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR("the heartbeat thread failed a step toward master (peer {}): "
                                 "unknown exception; the thread keeps running",
                                 peer.value);
                }
                lock.lock();

                // A stopped transport reports no loss, so the connection is forgotten here.
                if (action == Action::StopTransport)
                    forget_connection();
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
