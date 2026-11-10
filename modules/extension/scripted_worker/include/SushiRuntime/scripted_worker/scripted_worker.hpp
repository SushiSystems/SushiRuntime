/**************************************************************************/
/* scripted_worker.hpp                                                   */
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
 * @file scripted_worker.hpp
 * @brief Declares ScriptedWorker, a worker endpoint that speaks the wire protocol as a test scripts.
 * @see modules/extension/scripted_worker/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/frame_transport/frame_sending.hpp>
#include <SushiRuntime/in_process_transport/in_process_transport.hpp>
#include <SushiRuntime/scripted_worker/scripted_frames.hpp>
#include <SushiRuntime/link_security/handshake.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/transport/message_type.hpp>
#include <SushiRuntime/transport/transport.hpp>
#include <SushiRuntime/wire/wire_messages.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /** @brief Returns handshake octets as the bytes a wire message carries. */
        inline std::array<std::byte, Wire::K_NONCE_BYTES> to_wire(
            const std::array<std::uint8_t, Wire::K_NONCE_BYTES>& octets)
        {
            std::array<std::byte, Wire::K_NONCE_BYTES> bytes{};
            std::transform(octets.begin(), octets.end(), bytes.begin(),
                           [](std::uint8_t octet) { return std::byte{octet}; });
            return bytes;
        }

        /** @brief Returns the bytes of a wire nonce as handshake octets. */
        inline HandshakeNonce to_nonce(const std::array<std::byte, Wire::K_NONCE_BYTES>& bytes)
        {
            HandshakeNonce nonce{};
            std::transform(bytes.begin(), bytes.end(), nonce.bytes.begin(),
                           [](std::byte value) { return static_cast<std::uint8_t>(value); });
            return nonce;
        }

        /** @brief Names what a scripted worker does with a dispatch. */
        enum class DispatchAnswer : std::uint8_t
        {
            Accept,     /**< Confirms the dispatch and holds the task until the test answers. */
            Fail,       /**< Confirms the dispatch, then reports that the kernel failed. */
            FallSilent, /**< Sends nothing from the dispatch on, not even PONG. */
        };

        /**
         * @brief Registers with a master as a worker and answers dispatches as a test scripts.
         *
         * It speaks the wire protocol itself, with no WorkerAgent, and runs no kernel.
         */
        class ScriptedWorker final : public TransportEvents
        {
            public:
                /** @brief Builds a worker that claims a kernel for @p operation. */
                ScriptedWorker(InProcessFabric& fabric, OperationID operation, DispatchAnswer answer)
                    : transport_(InProcessTransport::make_worker(fabric, single_connection())),
                      operation_(operation), answer_(answer)
                {
                    worker_nonce_.bytes.fill(0x11);
                }

                /** @brief Stops the transport, after which no event reaches this object. */
                ~ScriptedWorker()
                {
                    transport_->stop();
                }

                ScriptedWorker(const ScriptedWorker&) = delete;
                ScriptedWorker& operator=(const ScriptedWorker&) = delete;
                ScriptedWorker(ScriptedWorker&&) = delete;
                ScriptedWorker& operator=(ScriptedWorker&&) = delete;

                /** @brief Sets the facts HELLO reports; call before start(). */
                void report(NodeFacts facts)
                {
                    facts_ = std::move(facts);
                }

                /** @brief Sets the task limit PROOF reports; call before start(). */
                void limit_jobs(std::uint32_t max_jobs) noexcept
                {
                    max_jobs_ = max_jobs;
                }

                /** @brief Starts the transport and returns whether it started. */
                [[nodiscard]] bool start()
                {
                    return transport_->start(*this).is_ok();
                }

                /** @brief Waits until @p count dispatches arrived, or @p limit passed. */
                [[nodiscard]] bool wait_for_dispatches(
                    std::size_t count, std::chrono::milliseconds limit = std::chrono::seconds(10))
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, limit,
                                             [&] { return dispatches_.size() >= count; });
                }

                /** @brief Returns the number of dispatches that arrived. */
                [[nodiscard]] std::size_t dispatch_count() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return dispatches_.size();
                }

                /**
                 * @brief Returns a copy of one dispatch.
                 * @pre @p index is below dispatch_count().
                 */
                [[nodiscard]] Wire::TaskDispatchMessage dispatch(std::size_t index) const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return dispatches_.at(index);
                }

                /** @brief Returns the number of master frames that did not decode. */
                [[nodiscard]] int undecodable_frames() const noexcept
                {
                    return undecodable_frames_.load(std::memory_order_relaxed);
                }

                /** @brief Returns the number of frames the transport did not queue. */
                [[nodiscard]] int refused_sends() const noexcept
                {
                    return refused_sends_.load(std::memory_order_relaxed);
                }

                /** @brief Hands the master a stream of the given frames. */
                [[nodiscard]] SendStatus send_stream(std::vector<OutboundFrame> frames)
                {
                    PeerID master;
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        master = master_;
                    }
                    return transport_->send_stream(
                        master, std::make_unique<ScriptedFrames>(std::move(frames)));
                }

                /** @brief Opens the handshake on a new connection. */
                void on_peer_connected(PeerID peer, const PeerDescription& /*info*/) override
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        master_ = peer;
                    }

                    Wire::HelloMessage hello;
                    hello.worker_nonce = to_wire(worker_nonce_.bytes);
                    hello.build_id = "scripted";
                    hello.node_name = facts_.name;
                    hello.operating_system = facts_.operating_system;
                    for (const NodeLabel& label : facts_.labels)
                        hello.labels.push_back(Wire::HelloLabel{label.key, label.value});
                    for (const DeviceFacts& device : facts_.devices)
                        hello.devices.push_back(
                            Wire::HelloDevice{device.kind, device.name, device.memory_bytes});
                    send(peer, hello);
                }

                /** @brief Answers CHALLENGE, PING and TASK_DISPATCH until the worker falls silent. */
                void on_frame(PeerID peer, MessageType type, std::uint8_t /*flags*/,
                              span<const std::byte> payload) override
                {
                    if (is_silent())
                        return;

                    if (type == MessageType::Challenge)
                        answer_challenge(peer, payload);
                    else if (type == MessageType::Ping)
                        answer_ping(peer, payload);
                    else if (type == MessageType::TaskDispatch)
                        take_dispatch(peer, payload);
                }

                /** @brief Ignores a loss; the transport does not reconnect. */
                void on_peer_lost(PeerID /*peer*/, const PeerLoss& /*loss*/) override
                {

                }

                /** @brief Ignores the end of connecting, which follows every loss here. */
                void on_connect_abandoned(const std::string& /*cause*/) override
                {

                }

            private:
                /** @brief Returns options under which a lost connection is not opened again. */
                [[nodiscard]] static InProcessWorkerOptions single_connection()
                {
                    InProcessWorkerOptions options;
                    options.reconnect = false;
                    return options;
                }

                /** @brief Returns whether a FallSilent worker has taken its dispatch. */
                [[nodiscard]] bool is_silent() const
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    return silent_;
                }

                /** @brief Queues one control frame and counts it when the transport refuses. */
                template <typename Message>
                void send(PeerID peer, const Message& message)
                {
                    if (send_message(*transport_, peer, message) != SendStatus::Queued)
                        refused_sends_.fetch_add(1, std::memory_order_relaxed);
                }

                /** @brief Decodes a payload, or records a test failure and returns false. */
                template <typename Message>
                [[nodiscard]] bool decode(span<const std::byte> payload, Message& message)
                {
                    if (Message::decode(payload, message).ok())
                        return true;

                    undecodable_frames_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }

                /** @brief Proves the empty secret and names the operation the worker claims. */
                void answer_challenge(PeerID peer, span<const std::byte> payload)
                {
                    Wire::ChallengeMessage challenge;
                    if (!decode(payload, challenge))
                        return;

                    Wire::ProofMessage proof;
                    proof.worker_proof = to_wire(
                        compute_proof(HandshakeRole::Worker, span<const std::uint8_t>(),
                                      worker_nonce_, to_nonce(challenge.master_nonce)));
                    proof.ops = {operation_};
                    proof.max_jobs = max_jobs_;
                    send(peer, proof);
                }

                /** @brief Echoes the token of a PING. */
                void answer_ping(PeerID peer, span<const std::byte> payload)
                {
                    Wire::PingMessage ping;
                    if (!decode(payload, ping))
                        return;

                    Wire::PongMessage pong;
                    pong.token = ping.token;
                    send(peer, pong);
                }

                /** @brief Records a dispatch, wakes the test and answers as scripted. */
                void take_dispatch(PeerID peer, span<const std::byte> payload)
                {
                    Wire::TaskDispatchMessage dispatch;
                    if (!decode(payload, dispatch))
                        return;

                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        silent_ = answer_ == DispatchAnswer::FallSilent;
                        dispatches_.push_back(dispatch);
                    }
                    changed_.notify_all();

                    if (answer_ == DispatchAnswer::FallSilent)
                        return;

                    Wire::TaskAcceptedMessage accepted;
                    accepted.task_id = dispatch.task_id;
                    send(peer, accepted);

                    if (answer_ != DispatchAnswer::Fail)
                        return;

                    Wire::TaskResultMessage result;
                    result.task_id = dispatch.task_id;
                    result.status = Wire::TaskResultStatus::KernelFailed;
                    result.outputs = dispatch.outputs;
                    send(peer, result);
                }

                /// @brief The worker endpoint; this object is its only event receiver.
                std::shared_ptr<InProcessTransport> transport_;

                /// @brief The operation the worker says it has a kernel for.
                OperationID operation_;

                /// @brief What the worker does with a dispatch.
                DispatchAnswer answer_;

                /// @brief The nonce the worker sends in HELLO.
                HandshakeNonce worker_nonce_{};

                /// @brief The task limit PROOF reports.
                std::uint32_t max_jobs_ = 16;

                /// @brief The facts HELLO reports; HELLO's views point into them.
                NodeFacts facts_{"scripted-worker", OperatingSystem::Unknown, {}, {}};

                /// @brief Guards master_, silent_ and dispatches_.
                mutable std::mutex mutex_;

                /// @brief Wakes wait_for_dispatches() on every dispatch.
                std::condition_variable changed_;

                /// @brief The master's peer id on the open connection.
                PeerID master_;

                /// @brief Set when a FallSilent worker took its dispatch.
                bool silent_ = false;

                /// @brief The dispatches that arrived, in order.
                std::vector<Wire::TaskDispatchMessage> dispatches_;

                /// @brief Frames the transport did not queue.
                std::atomic<int> refused_sends_{0};

                /// @brief Master frames of a type the worker reads that did not decode.
                std::atomic<int> undecodable_frames_{0};
        };

    } // namespace Distributed
} // namespace SushiRuntime
