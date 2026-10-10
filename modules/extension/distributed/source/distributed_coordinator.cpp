/**************************************************************************/
/* distributed_coordinator.cpp                                            */
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
 * @file distributed_coordinator.cpp
 * @brief Implements DistributedCoordinator: the master's bricks wired behind the offload seam.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include "distributed_coordinator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/distributed/message_type.hpp>

#include "buffer_exchange.hpp"
#include "cluster_monitor.hpp"
#include "frame_sending.hpp"
#include "handshake.hpp"
#include "message_router.hpp"
#include "offload_dispatcher.hpp"
#include "pending_tasks.hpp"
#include "task_resubmission.hpp"
#include "wire_codec.hpp"
#include "wire_frame.hpp"
#include "wire_messages.hpp"
#include "worker_directory.hpp"

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the words a log line uses for each PeerLossCause, by its value. */
            constexpr const char* K_LOSS_CAUSE_WORDS[] = {
                "closed by the peer",      "closed by this master", "read or write failed",
                "write stalled",           "not a SushiRuntime frame",
                "protocol version mismatch", "malformed frame",     "a frame handler failed",
            };

            /** @brief Holds the words a log line uses for each GoodbyeReason, by its value. */
            constexpr const char* K_GOODBYE_REASON_WORDS[] = {
                "unknown reason", "shutdown", "liveness", "detach"};

            /** @brief Holds the words a log line uses for each TaskResultStatus, by its value. */
            constexpr const char* K_RESULT_STATUS_WORDS[] = {
                "unknown status",   "succeeded",         "the kernel failed",
                "an output could not be allocated",      "an input did not arrive",
                "no kernel for the operation",           "the worker is shutting down",
            };

            /** @brief Returns the entry of a word table for a wire value, or its first entry. */
            template <std::size_t Count>
            const char* words_for(const char* const (&table)[Count], std::size_t value) noexcept
            {
                return table[value < Count ? value : 0];
            }

            /** @brief Returns 32 octets of a nonce or a proof as the wire carries them. */
            std::array<std::byte, Wire::K_NONCE_BYTES> to_wire(
                const std::array<std::uint8_t, Wire::K_NONCE_BYTES>& octets) noexcept
            {
                std::array<std::byte, Wire::K_NONCE_BYTES> wire{};
                for (std::size_t index = 0; index < wire.size(); ++index)
                    wire[index] = static_cast<std::byte>(octets[index]);
                return wire;
            }

            /** @brief Returns the monotonic clock in microseconds, the token of a PING. */
            std::uint64_t monotonic_micros() noexcept
            {
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
            }

            /** @brief Returns a count of milliseconds as the u32 a WELCOME field carries. */
            std::uint32_t to_wire_milliseconds(std::chrono::milliseconds duration) noexcept
            {
                const std::int64_t upper = std::numeric_limits<std::uint32_t>::max();
                return static_cast<std::uint32_t>(
                    std::clamp<std::int64_t>(duration.count(), 0, upper));
            }

            /** @brief Returns the session identifier of one coordinator, drawn at random. */
            std::uint64_t draw_session_id()
            {
                const HandshakeNonce nonce = make_nonce();
                std::uint64_t session = 0;
                for (std::size_t index = 0; index < sizeof(session); ++index)
                    session |= static_cast<std::uint64_t>(nonce.bytes[index]) << (8 * index);
                return session;
            }

            /** @brief Returns the given policy, or LeastLoadedOffloadPolicy when it is null. */
            std::shared_ptr<IOffloadPolicy> policy_or_default(
                std::shared_ptr<IOffloadPolicy> policy)
            {
                if (policy)
                    return policy;
                return std::make_shared<LeastLoadedOffloadPolicy>();
            }

            /**
             * @brief Returns the transport after checking it is there.
             * @throws Core::SushiException When @p transport is null.
             */
            std::shared_ptr<ITransport> require_transport(std::shared_ptr<ITransport> transport)
            {
                SR_THROW_IF(!transport,
                            "DistributedCoordinator needs a master-side transport; got null.");
                return transport;
            }

            /** @brief Returns the monitor's timings out of the cluster's options. */
            ClusterMonitorOptions monitor_options(const ClusterOptions& options)
            {
                ClusterMonitorOptions timings;
                timings.liveness_timeout = options.liveness_timeout;
                timings.task_deadline = options.task_deadline;
                timings.handshake_timeout = options.handshake_timeout;
                return timings;
            }

            /** @brief Returns the cause of a loss in words, with the transport's detail. */
            std::string describe(const PeerLoss& loss)
            {
                std::string words =
                    words_for(K_LOSS_CAUSE_WORDS, static_cast<std::size_t>(loss.cause));
                if (!loss.text.empty())
                    words += " (" + loss.text + ")";
                return words;
            }
        } // namespace

        /**
         * @brief Holds the master's bricks and the handlers that call them in turn.
         *
         * It is the transport's event receiver. Each handler runs on the reader thread of the
         * peer that sent the frame and holds no lock of its own between two brick calls.
         */
        struct DistributedCoordinator::Impl final : public TransportEvents
        {
            /** @brief Names the router of one frame: peer, flags, payload. */
            using Router =
                Wire::MessageRouter<PeerId, std::uint8_t, SushiRuntime::span<const std::byte>>;

            /** @brief Describes what retire_peer() found and returned to the scheduler. */
            struct RetiredPeer
            {
                /** @brief What the directory knew about the peer. */
                RemovedPeer peer;

                /** @brief The tasks of the worker that went back to the scheduler. */
                std::size_t tasks_returned = 0;
            };

            /** @brief Builds the bricks in the order they are declared and sets the handlers. */
            Impl(BufferRegistry& shared_buffers, std::shared_ptr<ITransport> master_transport,
                 ClusterOptions cluster_options)
                : options(std::move(cluster_options)),
                  registry(shared_buffers),
                  transport(require_transport(std::move(master_transport))),
                  directory(options.secret.bytes(), policy_or_default(options.policy)),
                  exchange(registry, options.chunk_bytes),
                  dispatcher(registry, directory, pending, *transport),
                  monitor(directory, pending, registry, exchange, *transport,
                          monitor_options(options)),
                  session_id(draw_session_id())
            {
                handle(&Impl::on_hello);
                handle(&Impl::on_proof);
                handle(&Impl::on_goodbye);
                handle(&Impl::on_task_accepted);
                handle(&Impl::on_task_result);
                handle(&Impl::on_buffer_request);
                handle(&Impl::on_buffer_chunk);
                handle(&Impl::on_buffer_abort);
                handle(&Impl::on_pong);
                handle(&Impl::on_heartbeat);
            }

            /**
             * @brief Sets the handler of one message type: decode, then call a member.
             *
             * A payload that does not decode closes the peer and reaches no member.
             * @tparam Message A wire message: it names its type as K_TYPE and has decode().
             */
            template <typename Message>
            void handle(void (Impl::*member)(PeerId, std::uint8_t, const Message&))
            {
                router.set_handler(
                    Message::K_TYPE,
                    [this, member](PeerId peer, std::uint8_t flags,
                                   SushiRuntime::span<const std::byte> payload)
                    {
                        Message message;
                        const Wire::DecodeResult decoded = Message::decode(payload, message);
                        if (decoded.ok())
                        {
                            (this->*member)(peer, flags, message);
                            return;
                        }

                        drop_peer(peer,
                                  Core::Detail::sr_format(
                                      "sent a malformed {} frame ({} bytes): decoding stopped "
                                      "at offset {} with status {}",
                                      message_type_name(Message::K_TYPE), payload.size(),
                                      decoded.offset, static_cast<int>(decoded.status)));
                    });
            }

            /** @brief Adds a connection that owes HELLO to the directory. */
            void on_peer_connected(PeerId peer, const PeerInfo& info) override
            {
                directory.add_peer(peer, info.endpoint, std::chrono::steady_clock::now());
                SR_LOG_DEBUG("peer {} connected from {}; it owes HELLO", peer.value,
                             info.endpoint);
            }

            /** @brief Refreshes the peer's liveness and routes a frame the directory admits. */
            void on_frame(PeerId peer, MessageType type, std::uint8_t flags,
                          SushiRuntime::span<const std::byte> payload) override
            {
                if (!directory.admits(peer, type))
                    return;

                directory.note_frame(peer, std::chrono::steady_clock::now());
                if (router.route(type, peer, flags, payload) == Wire::RouteStatus::Unhandled)
                    drop_peer(peer, Core::Detail::sr_format(
                                        "sent {}, which a master does not take",
                                        message_type_name(type)));
            }

            /** @brief Returns a lost worker's tasks to the scheduler and logs the loss once. */
            void on_peer_lost(PeerId peer, const PeerLoss& loss) override
            {
                if (loss.cause == PeerLossCause::VersionMismatch)
                    rejected_version.fetch_add(1, std::memory_order_relaxed);

                const RetiredPeer retired = retire_peer(peer);
                if (!retired.peer.known)
                    return;

                if (loss.cause == PeerLossCause::VersionMismatch)
                {
                    SR_LOG_ERROR("{} (peer {}) speaks protocol v{}, this master v{}; build both "
                                 "from one commit; closed, {} tasks returned to the scheduler",
                                 retired.peer.endpoint, peer.value, loss.peer_version,
                                 Wire::K_PROTOCOL_VERSION, retired.tasks_returned);
                    return;
                }

                log_retired(peer, retired, describe(loss),
                            loss.cause == PeerLossCause::ClosedLocally);
            }

            /** @brief Records that a transport gave up connecting; a master's never does. */
            void on_connect_abandoned(const std::string& cause) override
            {
                SR_LOG_DEBUG("a master-side transport reported an abandoned connect: {}", cause);
            }

            /** @brief Answers HELLO with CHALLENGE when the directory expects it. */
            void on_hello(PeerId peer, std::uint8_t /*flags*/, const Wire::HelloMessage& hello)
            {
                const HandshakeNonce master_nonce = make_nonce();
                const HelloReply reply = directory.accept_hello(peer, hello, master_nonce);
                if (reply.step != HandshakeStep::Accepted)
                    return;

                SR_LOG_DEBUG("peer {} says HELLO as \"{}\", build {}", peer.value,
                             hello.node_name, hello.build_id);

                Wire::ChallengeMessage challenge;
                challenge.master_nonce = to_wire(master_nonce.bytes);
                challenge.master_proof = to_wire(reply.master_proof);
                challenge.session_id = session_id;
                send_control(peer, challenge);
            }

            /** @brief Answers PROOF with WELCOME, or with REJECT and a close. */
            void on_proof(PeerId peer, std::uint8_t /*flags*/, const Wire::ProofMessage& proof)
            {
                const std::chrono::steady_clock::time_point now =
                    std::chrono::steady_clock::now();
                const HandshakeStep step = directory.accept_proof(peer, proof, now);
                if (step == HandshakeStep::Accepted)
                {
                    Wire::WelcomeMessage welcome;
                    welcome.worker_id = peer.value;
                    welcome.liveness_timeout_ms = to_wire_milliseconds(monitor.liveness_timeout());
                    welcome.ping_interval_ms = to_wire_milliseconds(monitor.period());
                    welcome.chunk_bytes = options.chunk_bytes;
                    send_control(peer, welcome);
                    log_registered(peer, proof.ops.size(), now);
                    return;
                }

                if (step != HandshakeStep::AuthenticationFailed)
                    return;

                rejected_authentication.fetch_add(1, std::memory_order_relaxed);
                Wire::RejectMessage reject;
                reject.reason = Wire::RejectReason::Authentication;
                reject.sender_version = Wire::K_PROTOCOL_VERSION;
                reject.text = "the proof does not match this master's secret";
                send_control(peer, reject);
                drop_peer(peer, "failed authentication");
            }

            /** @brief Returns a leaving worker's tasks at once and closes its connection. */
            void on_goodbye(PeerId peer, std::uint8_t /*flags*/,
                            const Wire::GoodbyeMessage& goodbye)
            {
                std::string cause = "said GOODBYE (";
                cause += words_for(K_GOODBYE_REASON_WORDS,
                                   static_cast<std::size_t>(goodbye.reason));
                cause += ")";
                if (!goodbye.text.empty())
                    cause.append(": ").append(goodbye.text);
                drop_peer(peer, cause);
            }

            /** @brief Records that a worker confirmed a dispatch. */
            void on_task_accepted(PeerId peer, std::uint8_t /*flags*/,
                                  const Wire::TaskAcceptedMessage& accepted)
            {
                pending.acknowledge(accepted.task_id, peer);
            }

            /**
             * @brief Settles the task of a result: completed when every output arrived whole,
             *        resubmitted otherwise. A result whose task is not pending changes nothing.
             */
            void on_task_result(PeerId peer, std::uint8_t /*flags*/,
                                const Wire::TaskResultMessage& result)
            {
                std::optional<PendingTask> task = pending.take(result.task_id, peer);
                if (!task.has_value())
                    return;

                const bool is_whole =
                    result.status == Wire::TaskResultStatus::Succeeded &&
                    exchange.finish_task(task->task_id,
                                         SushiRuntime::span<const BufferHandle>(
                                             task->outputs.data(), task->outputs.size()));
                if (is_whole)
                {
                    directory.release_inflight(peer);
                    dispatcher.complete_remote(*task->node);
                    sink->complete_remote(*task->node);
                    return;
                }

                const std::size_t resubmitted =
                    resubmit(SushiRuntime::span<PendingTask>(&*task, 1));
                SR_LOG_DEBUG("task {} came back from worker {} without a whole result ({}); {} "
                             "resubmitted",
                             result.task_id, peer.value,
                             words_for(K_RESULT_STATUS_WORDS,
                                       static_cast<std::size_t>(result.status)),
                             resubmitted);
            }

            /** @brief Hands the peer's writer a stream of the requested buffer version. */
            void on_buffer_request(PeerId peer, std::uint8_t /*flags*/,
                                   const Wire::BufferRequestMessage& request)
            {
                pending.note_chunk(peer, std::chrono::steady_clock::now());
                const SendStatus sent = transport->send_stream(
                    peer, exchange.serve_pull(request.id, request.version));
                if (sent != SendStatus::Queued)
                    SR_LOG_DEBUG("pull of buffer {} version {} by worker {} was not queued "
                                 "(status {}); the worker's pull timeout fails the task",
                                 request.id.value, request.version, peer.value,
                                 static_cast<int>(sent));
            }

            /** @brief Writes one output chunk in place when both halves of the fence pass. */
            void on_buffer_chunk(PeerId peer, std::uint8_t flags,
                                 const Wire::BufferChunkMessage& chunk)
            {
                if (!pending.expects_output(chunk.task_id, peer, chunk.handle.id,
                                            chunk.handle.version))
                {
                    fenced_chunks.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                pending.note_chunk(peer, std::chrono::steady_clock::now());
                const bool is_last = (flags & Wire::K_FRAME_FLAG_LAST_CHUNK) != 0;
                if (exchange.apply_output_chunk(peer, chunk, is_last) == ChunkStatus::Malformed)
                    drop_peer(peer, "sent a BUFFER_CHUNK whose purpose, size or offset is wrong");
            }

            /** @brief Forgets the progress of an output transfer its worker gave up. */
            void on_buffer_abort(PeerId peer, std::uint8_t /*flags*/,
                                 const Wire::BufferAbortMessage& abort)
            {
                if (pending.expects_output(abort.task_id, peer, abort.id, abort.version))
                    exchange.forget_task(abort.task_id);
            }

            /** @brief Records the round trip of a PING and the worker's free memory. */
            void on_pong(PeerId peer, std::uint8_t /*flags*/, const Wire::PongMessage& pong)
            {
                const std::uint64_t now = monotonic_micros();
                const std::uint64_t elapsed = now >= pong.token ? now - pong.token : 0;
                const std::uint64_t upper = std::numeric_limits<std::uint32_t>::max();
                directory.note_pong(peer, static_cast<std::uint32_t>(std::min(elapsed, upper)),
                                    pong.free_mem);
            }

            /** @brief Records a worker's load and which tasks it still holds. */
            void on_heartbeat(PeerId peer, std::uint8_t /*flags*/,
                              const Wire::HeartbeatMessage& heartbeat)
            {
                directory.note_heartbeat(peer, heartbeat.queue_depth, heartbeat.cache_bytes);
                pending.note_heartbeat(peer, heartbeat.oldest_held_task_id);
            }

            /** @brief Queues one control frame; a refusal is left to the peer's own timeouts. */
            template <typename Message>
            void send_control(PeerId peer, const Message& message)
            {
                const SendStatus sent = send_message(*transport, peer, message);
                if (sent != SendStatus::Queued)
                    SR_LOG_DEBUG("{} for peer {} was not queued (status {})",
                                 message_type_name(Message::K_TYPE), peer.value,
                                 static_cast<int>(sent));
            }

            /** @brief Passes taken tasks to resubmit_tasks() and counts them. */
            std::size_t resubmit(SushiRuntime::span<PendingTask> tasks) noexcept
            {
                if (tasks.empty() || sink == nullptr)
                    return 0;

                const std::size_t count =
                    resubmit_tasks(tasks, registry, exchange, directory, *sink);
                redispatched.fetch_add(count, std::memory_order_relaxed);
                return count;
            }

            /**
             * @brief Forgets a peer and, when it was a worker, returns its tasks.
             *
             * The directory forgets a peer once, so of two calls for one peer the second finds
             * it unknown and returns nothing.
             */
            RetiredPeer retire_peer(PeerId peer)
            {
                RetiredPeer retired;
                retired.peer = directory.remove_peer(peer);
                if (!retired.peer.registered)
                    return retired;

                std::vector<PendingTask> tasks;
                pending.take_for_worker(peer, tasks);
                retired.tasks_returned =
                    resubmit(SushiRuntime::span<PendingTask>(tasks.data(), tasks.size()));
                peers_lost.fetch_add(1, std::memory_order_relaxed);
                return retired;
            }

            /** @brief Logs one line for a peer retire_peer() forgot. */
            void log_retired(PeerId peer, const RetiredPeer& retired, const std::string& cause,
                             bool is_logged_elsewhere)
            {
                if (retired.peer.registered)
                {
                    SR_LOG_WARN("worker {} \"{}\" {} lost: {}; {} tasks returned to the "
                                "scheduler",
                                peer.value, retired.peer.name, retired.peer.endpoint, cause,
                                retired.tasks_returned);
                    return;
                }

                if (is_logged_elsewhere)
                    SR_LOG_DEBUG("{} (peer {}) closed before it registered: {}",
                                 retired.peer.endpoint, peer.value, cause);
                else
                    SR_LOG_WARN("{} (peer {}) closed before it registered: {}",
                                retired.peer.endpoint, peer.value, cause);
            }

            /** @brief Closes a peer for a stated cause, returns its tasks and logs one line. */
            void drop_peer(PeerId peer, const std::string& cause)
            {
                // Sends to the peer fail from here, so a dispatch racing this is undone.
                transport->close_peer(peer);

                const RetiredPeer retired = retire_peer(peer);
                if (retired.peer.known)
                    log_retired(peer, retired, cause, false);
            }

            /** @brief Logs the line of a worker that has just registered. */
            void log_registered(PeerId peer, std::size_t kernel_count,
                                std::chrono::steady_clock::time_point now)
            {
                std::vector<WorkerSnapshot> workers;
                directory.snapshot(now, workers);
                for (const WorkerSnapshot& worker : workers)
                {
                    if (worker.id != peer.value)
                        continue;

                    SR_LOG_INFO("worker {} \"{}\" {} registered, {} kernels", worker.id,
                                worker.name, worker.endpoint, kernel_count);
                    return;
                }
            }

            /// @brief The secret, the policy, the kernels kept alive and the timings.
            ClusterOptions options;

            /// @brief Names and versions of the shared buffers; owned by Cluster::State.
            BufferRegistry& registry;

            /// @brief Carries every frame; this object is its only event receiver.
            std::shared_ptr<ITransport> transport;

            /// @brief Sessions, handshake state and the load of each worker.
            WorkerDirectory directory;

            /// @brief The tasks out on a worker and the nodes they own.
            PendingTasks pending;

            /// @brief Pull streams and the progress of incoming outputs.
            BufferExchange exchange;

            /// @brief Marks and dispatches nodes.
            OffloadDispatcher dispatcher;

            /// @brief The thread for pings, liveness and deadlines.
            ClusterMonitor monitor;

            /// @brief The handler of each message type a master takes.
            Router router;

            /// @brief Identifies this coordinator to workers that reconnect.
            std::uint64_t session_id;

            /// @brief Takes nodes back; set by bind() before any thread starts.
            Graph::OffloadSink* sink = nullptr;

            /// @brief Set by the first start().
            std::atomic<bool> started{false};

            /// @brief Set by the first stop().
            std::atomic<bool> stopped{false};

            /// @brief Tasks resubmitted by a handler or by stop().
            std::atomic<std::uint64_t> redispatched{0};

            /// @brief Output chunks refused because their task was not pending.
            std::atomic<std::uint64_t> fenced_chunks{0};

            /// @brief Peers whose proof failed.
            std::atomic<std::uint64_t> rejected_authentication{0};

            /// @brief Peers the transport closed for another protocol version.
            std::atomic<std::uint64_t> rejected_version{0};

            /// @brief Registered workers that left.
            std::atomic<std::uint64_t> peers_lost{0};
        };

        DistributedCoordinator::DistributedCoordinator(BufferRegistry& registry,
                                                       std::shared_ptr<ITransport> transport,
                                                       ClusterOptions options)
            : impl_(std::make_unique<Impl>(registry, std::move(transport), std::move(options)))
        {

        }

        DistributedCoordinator::~DistributedCoordinator()
        {
            stop();
        }

        void DistributedCoordinator::bind(Graph::OffloadSink& sink)
        {
            impl_->sink = &sink;
        }

        TransportResult DistributedCoordinator::start()
        {
            Impl& impl = *impl_;
            TransportResult result;
            if (impl.sink == nullptr)
            {
                result.status = TransportStatus::Failed;
                result.message = "the coordinator has no sink; bind() must run before start()";
                return result;
            }
            if (impl.started.exchange(true, std::memory_order_acq_rel))
            {
                result.status = TransportStatus::AlreadyStarted;
                result.message = "the coordinator was started before; a master starts once";
                return result;
            }

            result = impl.transport->start(impl);
            if (!result.is_ok())
            {
                SR_LOG_ERROR("distributed master did not start: {}; nothing is left running",
                             result.message);
                return result;
            }

            std::string failure;
            try
            {
                impl.monitor.start(*impl.sink);
            }
            catch (const std::exception& error)
            {
                failure = error.what();
            }
            catch (...)
            {
                failure = "unknown exception";
            }

            if (!failure.empty())
            {
                impl.transport->stop();
                result.status = TransportStatus::Failed;
                result.message = "cannot start the cluster monitor thread: " + failure;
                SR_LOG_ERROR("distributed master did not start: {}; the transport is stopped",
                             result.message);
                return result;
            }

            const bool is_network = impl.transport->exposure() == TransportExposure::Network;
            SR_LOG_INFO("distributed master listening on a {} transport, protocol v{}, "
                        "authentication {}",
                        is_network ? "network" : "loopback", Wire::K_PROTOCOL_VERSION,
                        impl.options.secret.empty() ? "off" : "on");
            return result;
        }

        void DistributedCoordinator::stop() noexcept
        {
            Impl& impl = *impl_;
            if (impl.stopped.exchange(true, std::memory_order_acq_rel))
                return;

            impl.pending.close();
            impl.directory.close();
            impl.monitor.stop();
            impl.transport->stop();

            std::vector<PendingTask> tasks;
            try
            {
                impl.pending.drain(tasks);
            }
            catch (const std::exception& error)
            {
                SR_LOG_ERROR("distributed master could not drain every pending task: {}; the "
                             "nodes not drained stay with the table and do not run",
                             error.what());
            }
            catch (...)
            {
                SR_LOG_ERROR("distributed master could not drain every pending task: unknown "
                             "exception; the nodes not drained stay with the table and do not "
                             "run");
            }

            // Every reader and the monitor are joined, so this thread alone holds the tasks.
            const std::size_t returned =
                impl.resubmit(SushiRuntime::span<PendingTask>(tasks.data(), tasks.size()));
            SR_LOG_INFO("distributed master stopped; {} tasks returned to the scheduler",
                        returned);
        }

        void DistributedCoordinator::prepare(span<const sushi_ptr<Graph::Node>> nodes)
        {
            impl_->dispatcher.prepare(nodes);
        }

        bool DistributedCoordinator::try_offload(Graph::Node& node)
        {
            return impl_->dispatcher.try_offload(node);
        }

        void DistributedCoordinator::on_local_completion(Graph::Node& node) noexcept
        {
            impl_->dispatcher.complete_local(node);
        }

        void DistributedCoordinator::share(void* address, std::size_t bytes)
        {
            impl_->registry.register_buffer(address, bytes);
        }

        Graph::OffloadStatistics DistributedCoordinator::statistics() const noexcept
        {
            const Impl& impl = *impl_;
            const OffloadDispatcherStatistics dispatched = impl.dispatcher.statistics();

            Graph::OffloadStatistics snapshot;
            snapshot.offloaded = dispatched.offloaded;
            snapshot.declined = dispatched.declined;
            snapshot.redispatched = impl.monitor.statistics().redispatched +
                                    impl.redispatched.load(std::memory_order_relaxed);
            snapshot.stale_writebacks = impl.exchange.statistics().stale_writebacks +
                                        impl.fenced_chunks.load(std::memory_order_relaxed);
            snapshot.alive_workers = impl.directory.alive_count();
            return snapshot;
        }

        void DistributedCoordinator::on_release(void* address) noexcept
        {
            Impl& impl = *impl_;
            const GlobalBufferID released = impl.registry.unregister_buffer(address);
            if (!released.valid())
                return;

            try
            {
                Wire::BufferReleaseMessage release;
                release.id = released;

                std::vector<PeerId> workers;
                impl.directory.list_workers(workers);
                for (const PeerId worker : workers)
                    impl.send_control(worker, release);
            }
            catch (const std::exception& error)
            {
                SR_LOG_DEBUG("BUFFER_RELEASE for buffer {} was not sent: {}; the workers' "
                             "cache budgets evict it",
                             released.value, error.what());
            }
            catch (...)
            {
                SR_LOG_DEBUG("BUFFER_RELEASE for buffer {} was not sent: unknown exception; the "
                             "workers' cache budgets evict it",
                             released.value);
            }
        }

        ClusterStatistics DistributedCoordinator::cluster_statistics() const
        {
            const Impl& impl = *impl_;
            const OffloadDispatcherStatistics dispatched = impl.dispatcher.statistics();
            const ClusterMonitorStatistics monitored = impl.monitor.statistics();
            const BufferExchangeStatistics exchanged = impl.exchange.statistics();
            const TransportStatistics moved = impl.transport->statistics();

            ClusterStatistics snapshot;
            snapshot.offloaded = dispatched.offloaded;
            snapshot.declined = dispatched.declined;
            snapshot.redispatched =
                monitored.redispatched + impl.redispatched.load(std::memory_order_relaxed);
            snapshot.stale_writebacks =
                exchanged.stale_writebacks + impl.fenced_chunks.load(std::memory_order_relaxed);
            snapshot.handshake_rejected_auth =
                impl.rejected_authentication.load(std::memory_order_relaxed);
            snapshot.handshake_rejected_version =
                impl.rejected_version.load(std::memory_order_relaxed);
            snapshot.peers_lost = impl.peers_lost.load(std::memory_order_relaxed);
            snapshot.tasks_reaped_deadline = monitored.tasks_reaped_deadline;
            snapshot.tasks_reaped_unacknowledged = monitored.tasks_reaped_unacknowledged;
            snapshot.tasks_reaped_lost_result = monitored.tasks_reaped_lost_result;
            snapshot.bytes_sent = moved.bytes_sent;
            snapshot.bytes_received = moved.bytes_received;
            snapshot.chunks_sent = exchanged.chunks_sent;
            snapshot.chunks_received = exchanged.chunks_received;
            snapshot.transfers_aborted = exchanged.transfers_aborted;
            snapshot.send_queue_full = dispatched.send_queue_full;
            snapshot.alive_workers = impl.directory.alive_count();
            snapshot.pending_tasks = impl.pending.size();
            impl.directory.snapshot(std::chrono::steady_clock::now(), snapshot.workers);
            return snapshot;
        }

        bool DistributedCoordinator::supports(Graph::OpID op) const
        {
            return impl_->directory.supports(op);
        }

        bool DistributedCoordinator::wait_for_workers(std::size_t count,
                                                      std::chrono::milliseconds timeout)
        {
            return impl_->directory.wait_for_workers(count, timeout);
        }

        void DistributedCoordinator::set_liveness_timeout(
            std::chrono::milliseconds timeout) noexcept
        {
            impl_->monitor.set_liveness_timeout(timeout);
        }

        void DistributedCoordinator::set_task_deadline(std::chrono::milliseconds deadline) noexcept
        {
            impl_->monitor.set_task_deadline(deadline);
        }

    } // namespace Distributed
} // namespace SushiRuntime
