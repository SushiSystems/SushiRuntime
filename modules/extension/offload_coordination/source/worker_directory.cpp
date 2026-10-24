/**************************************************************************/
/* worker_directory.cpp                                                   */
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
 * @file worker_directory.cpp
 * @brief Implements WorkerDirectory, the master's record of each peer's session and load.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#include <SushiRuntime/offload_coordination/worker_directory.hpp>

#include <algorithm>
#include <array>
#include <utility>

#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/offload_coordination/task_description.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Bounds a wait so that adding it to the clock's reading cannot overflow. */
            constexpr std::chrono::hours K_LONGEST_WAIT{24 * 365};

            /** @brief Returns the bytes of a wire field as the octets the handshake works on. */
            template <std::size_t Count>
            std::array<std::uint8_t, Count> to_octets(
                const std::array<std::byte, Count>& bytes) noexcept
            {
                std::array<std::uint8_t, Count> octets{};
                std::transform(bytes.begin(), bytes.end(), octets.begin(),
                               [](std::byte byte) { return std::to_integer<std::uint8_t>(byte); });
                return octets;
            }

            /** @brief Returns the facts a HELLO carries, with every view copied. */
            NodeFacts to_facts(const Wire::HelloMessage& hello)
            {
                NodeFacts facts;
                facts.name.assign(hello.node_name);
                facts.operating_system = hello.operating_system;
                facts.labels.reserve(hello.labels.size());
                for (const Wire::HelloLabel& label : hello.labels)
                    facts.labels.push_back(
                        NodeLabel{std::string(label.key), std::string(label.value)});

                facts.devices.reserve(hello.devices.size());
                for (const Wire::HelloDevice& device : hello.devices)
                    facts.devices.push_back(
                        DeviceFacts{device.kind, std::string(device.name), device.memory_bytes});

                return facts;
            }
        } // namespace

        WorkerDirectory::WorkerDirectory(SushiRuntime::span<const std::uint8_t> secret,
                                         std::shared_ptr<OffloadPolicy> policy)
            : secret_(secret), policy_(std::move(policy))
        {

        }

        WorkerDirectory::~WorkerDirectory() = default;

        void WorkerDirectory::add_peer(PeerID peer, std::string endpoint,
                                       std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto [position, inserted] = sessions_.try_emplace(peer.value);
            if (!inserted)
                return;

            Session& session = position->second;
            session.endpoint = std::move(endpoint);
            session.connected_at = now;
            session.last_frame = now;
        }

        RemovedPeer WorkerDirectory::remove_peer(PeerID peer)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            RemovedPeer removed;
            const auto position = sessions_.find(peer.value);
            if (position == sessions_.end())
                return removed;

            Session& session = position->second;
            retire(session);
            removed.known = true;
            removed.registered = session.phase == Phase::Registered;
            removed.name = std::move(session.name);
            removed.endpoint = std::move(session.endpoint);
            sessions_.erase(position);
            return removed;
        }

        bool WorkerDirectory::admits(PeerID peer, MessageType type) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(peer.value);
            if (position == sessions_.end())
                return false;

            const Phase phase = position->second.phase;
            if (type == MessageType::Goodbye || type == MessageType::Hello)
                return true;
            if (type == MessageType::Proof)
                return phase == Phase::AwaitsProof;

            return phase == Phase::Registered;
        }

        HelloReply WorkerDirectory::accept_hello(PeerID peer, const Wire::HelloMessage& hello,
                                                 const HandshakeNonce& master_nonce)
        {
            HandshakeNonce worker_nonce{};
            worker_nonce.bytes = to_octets(hello.worker_nonce);
            const Sha256Digest master_proof =
                compute_proof(HandshakeRole::Master, secret_, worker_nonce, master_nonce);

            const std::lock_guard<std::mutex> lock(mutex_);
            HelloReply reply;
            const auto position = sessions_.find(peer.value);
            if (position == sessions_.end())
                return reply;

            Session& session = position->second;
            if (session.phase == Phase::Registered)
            {
                reply.step = HandshakeStep::AlreadyRegistered;
                return reply;
            }

            session.name.assign(hello.node_name);
            session.pending_facts = to_facts(hello);
            session.build_id.assign(hello.build_id);
            session.worker_nonce = worker_nonce;
            session.master_nonce = master_nonce;
            session.phase = Phase::AwaitsProof;

            reply.step = HandshakeStep::Accepted;
            reply.master_proof = master_proof;
            return reply;
        }

        HandshakeStep WorkerDirectory::accept_proof(PeerID peer, const Wire::ProofMessage& proof,
                                                    std::chrono::steady_clock::time_point now)
        {
            HandshakeNonce worker_nonce{};
            HandshakeNonce master_nonce{};
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const auto position = sessions_.find(peer.value);
                if (position == sessions_.end())
                    return HandshakeStep::UnknownPeer;
                if (closed_ || position->second.phase != Phase::AwaitsProof)
                    return HandshakeStep::OutOfOrder;

                worker_nonce = position->second.worker_nonce;
                master_nonce = position->second.master_nonce;
            }

            const bool proven = verify_proof(HandshakeRole::Worker, secret_, worker_nonce,
                                             master_nonce, to_octets(proof.worker_proof));
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                const auto position = sessions_.find(peer.value);
                if (position == sessions_.end())
                    return HandshakeStep::UnknownPeer;

                // The proof was checked without the mutex; it counts only for these nonces.
                Session& session = position->second;
                if (closed_ || session.phase != Phase::AwaitsProof ||
                    session.worker_nonce.bytes != worker_nonce.bytes ||
                    session.master_nonce.bytes != master_nonce.bytes)
                    return HandshakeStep::OutOfOrder;

                if (!proven)
                {
                    // A refused proof spends its nonces: the next PROOF needs a HELLO before it.
                    session.phase = Phase::AwaitsHello;
                    return HandshakeStep::AuthenticationFailed;
                }

                session.operations = proof.ops;
                for (const OperationID operation : session.operations)
                    ++workers_per_operation_[operation.value];

                // Only a proven HELLO replaces the facts kept under the name.
                session.facts = &(facts_by_name_[session.name] = std::move(session.pending_facts));
                session.max_jobs = proof.max_jobs;
                session.last_frame = now;
                session.phase = Phase::Registered;
                session.alive = true;
                alive_count_.fetch_add(1, std::memory_order_relaxed);
            }

            registered_.notify_all();
            return HandshakeStep::Accepted;
        }

        void WorkerDirectory::note_frame(PeerID peer, std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(peer.value);
            if (position != sessions_.end())
                position->second.last_frame = std::max(position->second.last_frame, now);
        }

        void WorkerDirectory::note_pong(PeerID peer, std::uint32_t rtt_micros,
                                        std::uint64_t free_mem)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(peer.value);
            if (position == sessions_.end() || position->second.phase != Phase::Registered)
                return;

            position->second.rtt_micros = rtt_micros;
            position->second.free_mem = free_mem;
        }

        void WorkerDirectory::note_heartbeat(PeerID peer, std::uint32_t queue_depth,
                                             std::uint64_t cache_bytes)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(peer.value);
            if (position == sessions_.end() || position->second.phase != Phase::Registered)
                return;

            position->second.queue_depth = queue_depth;
            position->second.cache_bytes = cache_bytes;
        }

        std::optional<PeerID> WorkerDirectory::pick(const Internal::Graph::Node& node,
                                                    std::uint64_t estimated_bytes)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (closed_)
                return std::nullopt;

            const OperationID operation = node.metadata.op_id;
            candidates_.clear();
            for (const auto& [value, session] : sessions_)
            {
                if (!has_room(session))
                    continue;

                // A worker without the node's kernel would only send the task back.
                if (std::find(session.operations.begin(), session.operations.end(), operation) ==
                    session.operations.end())
                    continue;

                WorkerLoad stats;
                stats.worker_id = static_cast<int>(value);
                stats.alive = true;
                stats.inflight = session.inflight;
                stats.free_mem = session.free_mem;
                stats.rtt_micros = session.rtt_micros;
                stats.facts = session.facts;
                candidates_.push_back(stats);
            }

            if (candidates_.empty())
                return std::nullopt;

            const std::optional<int> choice = policy_->pick_worker(
                describe_task(node.metadata),
                SushiRuntime::span<const WorkerLoad>(candidates_.data(), candidates_.size()),
                estimated_bytes);
            if (!choice.has_value() || *choice < 0)
                return std::nullopt;

            // A policy may answer with an id it was not offered; such a pick counts as none.
            const auto offered = [choice](const WorkerLoad& stats)
            {
                return stats.worker_id == *choice;
            };
            if (std::none_of(candidates_.begin(), candidates_.end(), offered))
                return std::nullopt;

            const auto position = sessions_.find(static_cast<std::uint32_t>(*choice));
            if (position == sessions_.end())
                return std::nullopt;

            ++position->second.inflight;
            return PeerID{position->first};
        }

        void WorkerDirectory::release_inflight(PeerID worker) noexcept
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(worker.value);
            if (position != sessions_.end() && position->second.inflight > 0)
                --position->second.inflight;
        }

        void WorkerDirectory::take_silent(std::chrono::steady_clock::time_point now,
                                          std::chrono::milliseconds liveness,
                                          std::vector<PeerID>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [value, session] : sessions_)
            {
                if (!session.alive || now - session.last_frame <= liveness)
                    continue;

                out.push_back(PeerID{value});
                retire(session);
            }
        }

        void WorkerDirectory::take_handshake_overdue(std::chrono::steady_clock::time_point now,
                                                     std::chrono::milliseconds limit,
                                                     std::vector<PeerID>& out)
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [value, session] : sessions_)
            {
                if (session.phase == Phase::Registered || session.handshake_overdue_listed)
                    continue;
                if (now - session.connected_at <= limit)
                    continue;

                out.push_back(PeerID{value});
                session.handshake_overdue_listed = true;
            }
        }

        void WorkerDirectory::list_workers(std::vector<PeerID>& out) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [value, session] : sessions_)
            {
                if (session.alive)
                    out.push_back(PeerID{value});
            }
        }

        bool WorkerDirectory::supports(OperationID op) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            return workers_per_operation_.count(op.value) != 0;
        }

        std::size_t WorkerDirectory::alive_count() const noexcept
        {
            return alive_count_.load(std::memory_order_relaxed);
        }

        bool WorkerDirectory::wait_for_workers(std::size_t count,
                                               std::chrono::milliseconds timeout)
        {
            const auto reached = [this, count]
            {
                return alive_count_.load(std::memory_order_relaxed) >= count;
            };

            std::unique_lock<std::mutex> lock(mutex_);
            const std::chrono::milliseconds bounded =
                std::min<std::chrono::milliseconds>(timeout, K_LONGEST_WAIT);
            registered_.wait_for(lock, bounded, [this, &reached] { return closed_ || reached(); });
            return !closed_ && reached();
        }

        std::optional<WorkerIdentity> WorkerDirectory::identity(PeerID worker) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto position = sessions_.find(worker.value);
            if (position == sessions_.end() || position->second.phase != Phase::Registered)
                return std::nullopt;

            const Session& session = position->second;
            return WorkerIdentity{session.name, session.endpoint, session.build_id};
        }

        void WorkerDirectory::snapshot(std::chrono::steady_clock::time_point now,
                                       std::vector<WorkerSnapshot>& out) const
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [value, session] : sessions_)
            {
                if (session.phase != Phase::Registered || !session.alive)
                    continue;

                const auto silence = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - session.last_frame);

                WorkerSnapshot worker;
                worker.id = value;
                worker.name = session.name;
                worker.endpoint = session.endpoint;
                worker.inflight = session.inflight;
                worker.rtt_micros = session.rtt_micros;
                worker.free_mem = session.free_mem;
                worker.queue_depth = session.queue_depth;
                worker.cache_bytes = session.cache_bytes;
                worker.ms_since_last_frame = static_cast<std::uint64_t>(
                    std::max<std::chrono::milliseconds::rep>(silence.count(), 0));
                out.push_back(std::move(worker));
            }
        }

        void WorkerDirectory::close() noexcept
        {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
                for (auto& [value, session] : sessions_)
                    retire(session);
            }

            registered_.notify_all();
        }

        bool WorkerDirectory::has_room(const Session& session) noexcept
        {
            return session.alive && session.inflight < session.max_jobs;
        }

        void WorkerDirectory::retire(Session& session) noexcept
        {
            if (!session.alive)
                return;

            session.alive = false;
            alive_count_.fetch_sub(1, std::memory_order_relaxed);
            for (const OperationID operation : session.operations)
            {
                const auto count = workers_per_operation_.find(operation.value);
                if (--count->second == 0)
                    workers_per_operation_.erase(count);
            }
        }

    } // namespace Distributed
} // namespace SushiRuntime
