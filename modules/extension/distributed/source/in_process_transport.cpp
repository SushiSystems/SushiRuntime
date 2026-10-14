/**************************************************************************/
/* in_process_transport.cpp                                               */
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
 * @file in_process_transport.cpp
 * @brief Moves version 3 frames between endpoints of one process, through a fault policy.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#include <SushiRuntime/distributed/in_process_transport.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/frame_transport/outbox.hpp>
#include <SushiRuntime/wire/wire_frame.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Holds what the two ends of one connection share.
         *
         * The worker's pump fills the worker half, then the master's accept() fills the
         * master half under the master's mutex; the fields are constant from there on.
         * InProcessTransport names it a friend, so it has a name outside this file.
         */
        struct InProcessLink
        {
            /** @brief The fabric's index of the worker endpoint. */
            std::size_t worker_index = 0;

            /** @brief The endpoint of each side, master first. */
            std::array<std::weak_ptr<InProcessTransport::Impl>, 2> endpoints;

            /** @brief The id each side gave the connection. */
            std::array<PeerID, 2> ids;

            /** @brief Counts the frames waiting in each side's inbox. */
            std::array<std::atomic<std::uint32_t>, 2> backlog{};

            /** @brief Set by a side whose stream waits for the other side's backlog. */
            std::array<std::atomic<bool>, 2> is_throttled{};

            /** @brief Set once a side was told the connection ended. */
            std::array<std::atomic<bool>, 2> is_loss_sent{};
        };

        namespace
        {
            /** @brief Caps the frames FaultAction::Hold may park; one above the cap is dropped. */
            constexpr std::size_t K_MAX_HELD_FRAMES = 100000;

            /** @brief Caps the frames waiting in a receiver before a stream stops being pulled. */
            constexpr std::uint32_t K_STREAM_WINDOW_FRAMES = 16;

            /** @brief Names the endpoint a started master reports where TCP reports a port. */
            constexpr const char* K_MASTER_ENDPOINT_NAME = "in-process";

            /** @brief Names the two ends of a connection on the fabric. */
            enum class Side : std::uint8_t
            {
                Master, /**< The master endpoint. */
                Worker, /**< A worker endpoint. */
            };

            /** @brief Returns the array index of a side. */
            constexpr std::size_t index_of(Side side) noexcept
            {
                return static_cast<std::size_t>(side);
            }

            /** @brief Returns the other end of a connection. */
            constexpr Side other(Side side) noexcept
            {
                return side == Side::Master ? Side::Worker : Side::Master;
            }

            /** @brief Names the link as the rest of this file writes it. */
            using Link = InProcessLink;

            /** @brief Names what an endpoint's pump finds in its inbox. */
            enum class InboundKind : std::uint8_t
            {
                Connected, /**< A worker connected; only a master receives it. */
                Frame,     /**< One frame from the other end. */
                Lost,      /**< The other end closed, stopped or refused a frame. */
            };

            /** @brief Holds one inbox entry. */
            struct Inbound
            {
                InboundKind kind = InboundKind::Frame; /**< What the entry is. */
                std::shared_ptr<Link> link;            /**< The connection it belongs to. */
                OutboundFrame frame;                   /**< The frame of a Frame entry. */
                PeerLoss loss;                         /**< The cause of a Lost entry. */
            };

            /** @brief Holds one end of a connection: its id and what it owes the other end. */
            struct Connection
            {
                /** @brief Builds an open end with an empty outbox. */
                Connection(std::shared_ptr<Link> shared, PeerID peer, std::size_t capacity)
                    : link(std::move(shared)), id(peer), outbox(capacity)
                {

                }

                std::shared_ptr<Link> link; /**< What both ends share. */
                PeerID id;                  /**< The id this end's endpoint uses. */
                Outbox outbox;              /**< The frames and streams this end sends. */
            };

            /** @brief Holds a frame the fault policy parked. */
            struct HeldFrame
            {
                std::shared_ptr<Link> link; /**< The connection the frame travels on. */
                Side destination;           /**< The side that receives it. */
                OutboundFrame frame;        /**< The frame. */
            };

            /** @brief Logs an exception that escaped an event; the peer ends, not the process. */
            void log_handler_failure(PeerID peer, const std::string& name, const char* event,
                                     std::size_t length, const char* what)
            {
                SR_LOG_ERROR("handler of {} ({} bytes) from {} (peer {}) threw '{}'; "
                             "closing the peer, the process continues",
                             event, length, name, peer.value, what);
            }
        } // namespace

        /**
         * @brief Holds the routing state the endpoints of one fabric share.
         */
        struct InProcessFabric::Impl
        {
            /**
             * @brief Reads what one frame of a worker's link needs, under one lock.
             * @param is_cut Receives whether the worker is partitioned.
             */
            void read_fault_state(std::size_t worker_index, bool& is_cut,
                                  std::shared_ptr<const FaultPolicy>& active_policy) const
            {
                std::lock_guard<std::mutex> lock(mutex);
                is_cut = worker_index < is_partitioned.size() && is_partitioned[worker_index];
                active_policy = policy;
            }

            /** @brief Returns whether a worker's link is cut. */
            [[nodiscard]] bool is_cut(std::size_t worker_index) const
            {
                std::lock_guard<std::mutex> lock(mutex);
                return worker_index < is_partitioned.size() && is_partitioned[worker_index];
            }

            /** @brief Parks a frame until flush_held(); drops it above K_MAX_HELD_FRAMES. */
            void hold(std::shared_ptr<Link> link, Side destination, OutboundFrame&& frame)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (held.size() < K_MAX_HELD_FRAMES)
                    held.push_back(HeldFrame{std::move(link), destination, std::move(frame)});
            }

            /// @brief Guards every member below; never held across a call out of the fabric.
            mutable std::mutex mutex;

            /// @brief The master endpoint, while one lives.
            std::weak_ptr<InProcessTransport::Impl> master;

            /// @brief The worker endpoints in the order they were made.
            std::vector<std::weak_ptr<InProcessTransport::Impl>> workers;

            /// @brief Whether each worker's link is cut; parallel to workers.
            std::vector<bool> is_partitioned;

            /// @brief The policy frames are put to; null delivers every frame.
            std::shared_ptr<const FaultPolicy> policy;

            /// @brief The frames the policy parked, in the order they arrived.
            std::vector<HeldFrame> held;
        };

        /**
         * @brief Holds one endpoint: its connections, its inbox and the pump that serves both.
         *
         * One mutex guards the inbox, the connection table and the wake flag. It is never held
         * across an event, a fault policy, an outbox call or a call into another endpoint.
         */
        struct InProcessTransport::Impl : std::enable_shared_from_this<InProcessTransport::Impl>
        {
            /** @brief Names where the endpoint is in its one start and one stop. */
            enum class Phase : std::uint8_t
            {
                Idle,    /**< start() has not run. */
                Running, /**< start() returned Ok and stop() has not begun. */
                Stopped, /**< stop() has begun, or start() failed. */
            };

            /** @brief The clock the worker's connection attempts are timed on. */
            using Clock = std::chrono::steady_clock;

            /** @brief Builds an endpoint that is not started. */
            Impl(std::shared_ptr<InProcessFabric::Impl> shared, Side endpoint_side,
                 std::size_t index, std::size_t control_capacity,
                 const InProcessWorkerOptions& options)
                : fabric(std::move(shared)), side(endpoint_side), worker_index(index),
                  control_queue_frames(control_capacity), worker_options(options)
            {

            }

            /** @brief Returns the open connection of a peer, or null. */
            [[nodiscard]] std::shared_ptr<Connection> find(PeerID peer)
            {
                std::lock_guard<std::mutex> lock(mutex);
                const auto found = table.find(peer.value);
                return found == table.end() ? nullptr : found->second;
            }

            /** @brief Tells the pump that an outbox or the inbox has something for it. */
            void wake()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    has_work = true;
                }
                wake_signal.notify_one();
            }

            /** @brief Puts an entry in the inbox; an endpoint that does not run discards it. */
            void enqueue(Inbound&& item)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!is_accepting)
                        return;
                    if (item.kind == InboundKind::Frame)
                        item.link->backlog[index_of(side)].fetch_add(1);
                    inbox.push_back(std::move(item));
                    has_work = true;
                }
                wake_signal.notify_one();
            }

            /** @brief Puts an entry in the inbox of one side of a link, if that endpoint lives. */
            static void send_to(const std::shared_ptr<Link>& link, Side destination,
                                Inbound&& item)
            {
                const std::shared_ptr<Impl> endpoint =
                    link->endpoints[index_of(destination)].lock();
                if (endpoint != nullptr)
                    endpoint->enqueue(std::move(item));
            }

            /**
             * @brief Tells one side, once, that the connection ended.
             *
             * A cut link carries no such word, as a half-open TCP link carries none.
             */
            void tell_lost(const std::shared_ptr<Link>& link, Side destination, const char* text)
            {
                if (fabric->is_cut(link->worker_index) ||
                    link->is_loss_sent[index_of(destination)].exchange(true))
                    return;
                Inbound item;
                item.kind = InboundKind::Lost;
                item.link = link;
                item.loss.cause = PeerLossCause::ClosedByPeer;
                item.loss.text = text;
                send_to(link, destination, std::move(item));
            }

            /**
             * @brief Admits a worker's connection on a running master endpoint.
             * @param link Its worker half is filled; the master half is filled here.
             * @return False when the endpoint does not run.
             */
            [[nodiscard]] bool accept(const std::shared_ptr<Link>& link)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (!is_accepting)
                        return false;
                    const PeerID peer{next_peer.fetch_add(1)};
                    link->ids[index_of(Side::Master)] = peer;
                    link->endpoints[index_of(Side::Master)] = weak_from_this();
                    table.emplace(peer.value,
                                  std::make_shared<Connection>(link, peer, control_queue_frames));

                    Inbound item;
                    item.kind = InboundKind::Connected;
                    item.link = link;
                    inbox.push_back(std::move(item));
                    has_work = true;
                }
                wake_signal.notify_one();
                return true;
            }

            /** @brief Returns the name an event gives the other end of a link. */
            [[nodiscard]] std::string peer_name(const Link& link) const
            {
                return side == Side::Master
                           ? "in-process worker " + std::to_string(link.worker_index)
                           : std::string("in-process master");
            }

            /**
             * @brief Makes on_peer_connected(); a throw ends the connection with HandlerFailed.
             * @pre The caller is the pump and the connection is in local_peers.
             */
            void announce(const std::shared_ptr<Connection>& connection)
            {
                const std::string name = peer_name(*connection->link);
                PeerLoss loss;
                try
                {
                    events->on_peer_connected(connection->id, PeerDescription{name});
                    return;
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(connection->id, name, "the connection event", 0,
                                        error.what());
                    loss.text = error.what();
                }
                catch (...)
                {
                    log_handler_failure(connection->id, name, "the connection event", 0,
                                        "unknown");
                    loss.text = "an exception that is no std::exception";
                }
                loss.cause = PeerLossCause::HandlerFailed;
                lose(connection, loss, true);
            }

            /**
             * @brief Removes a connection, tells the receiver and, for a worker, plans the next.
             * @param tells_other False when the other end already knows the connection ended.
             * @pre The caller is the pump.
             */
            void lose(const std::shared_ptr<Connection>& connection, const PeerLoss& loss,
                      bool tells_other)
            {
                local_peers.erase(connection->id.value);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    table.erase(connection->id.value);
                }
                connection->outbox.close();
                if (tells_other)
                    tell_lost(connection->link, other(side), "the peer closed the connection");

                if (phase.load() != Phase::Running)
                    return;
                try
                {
                    events->on_peer_lost(connection->id, loss);
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(connection->id, peer_name(*connection->link),
                                        "the loss event", 0, error.what());
                }
                catch (...)
                {
                    log_handler_failure(connection->id, peer_name(*connection->link),
                                        "the loss event", 0, "unknown");
                }

                if (side != Side::Worker)
                    return;
                if (worker_options.reconnect)
                    next_attempt = Clock::now() + worker_options.reconnect_delay;
                else
                    abandon("the in-process master was lost and reconnecting is off");
            }

            /** @brief Makes on_connect_abandoned() once and ends the worker's attempts. */
            void abandon(const std::string& cause)
            {
                is_abandoned = true;
                try
                {
                    events->on_connect_abandoned(cause);
                }
                catch (const std::exception& error)
                {
                    SR_LOG_ERROR("handler of the abandoned connection of in-process worker {} "
                                 "threw '{}'; the endpoint stays idle",
                                 worker_index, error.what());
                }
                catch (...)
                {
                    SR_LOG_ERROR("handler of the abandoned connection of in-process worker {} "
                                 "threw an unknown exception; the endpoint stays idle",
                                 worker_index);
                }
            }

            /**
             * @brief Delivers one inbox entry as an event.
             * @pre The caller is the pump.
             */
            void deliver(Inbound& item)
            {
                const std::size_t self = index_of(side);
                const PeerID peer = item.link->ids[self];

                if (item.kind == InboundKind::Frame)
                {
                    item.link->backlog[self].fetch_sub(1);
                    if (item.link->is_throttled[index_of(other(side))].exchange(false))
                    {
                        const std::shared_ptr<Impl> sender =
                            item.link->endpoints[index_of(other(side))].lock();
                        if (sender != nullptr)
                            sender->wake();
                    }
                }

                if (item.kind == InboundKind::Connected)
                {
                    const std::shared_ptr<Connection> connection = find(peer);
                    if (connection == nullptr)
                        return;
                    local_peers.emplace(peer.value, connection);
                    announce(connection);
                    return;
                }

                const auto found = local_peers.find(peer.value);
                if (found == local_peers.end())
                {
                    if (item.kind == InboundKind::Frame)
                        tell_lost(item.link, other(side), "the connection is closed on this end");
                    return;
                }
                const std::shared_ptr<Connection> connection = found->second;
                if (item.kind == InboundKind::Lost)
                {
                    lose(connection, item.loss, false);
                    return;
                }

                const OutboundFrame& frame = item.frame;
                bytes_received.fetch_add(Wire::K_FRAME_HEADER_BYTES + frame.payload.size(),
                                         std::memory_order_relaxed);
                PeerLoss loss;
                try
                {
                    events->on_frame(peer, frame.type, frame.flags,
                                     {frame.payload.data(), frame.payload.size()});
                    return;
                }
                catch (const std::exception& error)
                {
                    log_handler_failure(peer, peer_name(*item.link),
                                        message_type_name(frame.type), frame.payload.size(),
                                        error.what());
                    loss.text = error.what();
                }
                catch (...)
                {
                    log_handler_failure(peer, peer_name(*item.link),
                                        message_type_name(frame.type), frame.payload.size(),
                                        "unknown");
                    loss.text = "an exception that is no std::exception";
                }
                loss.cause = PeerLossCause::HandlerFailed;
                lose(connection, loss, true);
            }

            /**
             * @brief Returns whether the other side holds a full window of undelivered frames.
             *
             * A true answer leaves is_throttled set, and the other side's pump wakes this one
             * when it delivers a frame.
             */
            [[nodiscard]] bool is_window_full(Link& link) const
            {
                const std::atomic<std::uint32_t>& backlog = link.backlog[index_of(other(side))];
                if (backlog.load() < K_STREAM_WINDOW_FRAMES)
                    return false;
                link.is_throttled[index_of(side)].store(true);
                if (backlog.load() >= K_STREAM_WINDOW_FRAMES)
                    return true;
                link.is_throttled[index_of(side)].store(false);
                return false;
            }

            /**
             * @brief Puts one outbound frame to the partition and the fault policy.
             * @param frame Moved from when the frame is delivered or held.
             * @pre The caller is the pump.
             */
            void route(const Connection& connection, OutboundFrame& frame)
            {
                bytes_sent.fetch_add(Wire::K_FRAME_HEADER_BYTES + frame.payload.size(),
                                     std::memory_order_relaxed);

                const std::shared_ptr<Link>& link = connection.link;
                bool is_cut = false;
                std::shared_ptr<const FaultPolicy> policy;
                fabric->read_fault_state(link->worker_index, is_cut, policy);
                if (is_cut)
                    return;

                FaultAction action = FaultAction::Deliver;
                if (policy != nullptr)
                {
                    FaultedFrame faulted;
                    faulted.direction = side == Side::Master ? FrameDirection::MasterToWorker
                                                             : FrameDirection::WorkerToMaster;
                    faulted.worker_index = link->worker_index;
                    faulted.type = frame.type;
                    faulted.flags = frame.flags;
                    faulted.payload = {frame.payload.data(), frame.payload.size()};
                    action = (*policy)(faulted);
                }

                Inbound item;
                item.kind = InboundKind::Frame;
                item.link = link;
                switch (action)
                {
                    case FaultAction::Drop:
                        return;
                    case FaultAction::Hold:
                        fabric->hold(link, other(side), std::move(frame));
                        return;
                    case FaultAction::Duplicate:
                        item.frame = frame;
                        send_to(link, other(side), Inbound(item));
                        break;
                    case FaultAction::Deliver:
                        item.frame = std::move(frame);
                        break;
                }
                send_to(link, other(side), std::move(item));
            }

            /**
             * @brief Moves at most one frame of each connection towards its other end.
             *
             * A control frame goes first; a stream is asked for a frame only while the other
             * end's window has room. A connection whose outbox is closed and empty is lost here.
             * @return True when a frame moved, so another round may find more.
             * @pre The caller is the pump.
             */
            [[nodiscard]] bool flush_round()
            {
                round.clear();
                for (const auto& item : local_peers)
                    round.push_back(item.second);

                bool has_moved = false;
                for (const std::shared_ptr<Connection>& connection : round)
                {
                    Outbox& outbox = connection->outbox;
                    if (outbox.control_depth() == 0 && outbox.stream_count() > 0 &&
                        is_window_full(*connection->link))
                        continue;

                    const PopStatus popped = outbox.pop(scratch, std::chrono::milliseconds(0));
                    if (popped == PopStatus::Frame)
                    {
                        route(*connection, scratch);
                        has_moved = true;
                    }
                    else if (popped == PopStatus::Closed)
                    {
                        lose(connection, PeerLoss{PeerLossCause::ClosedLocally, 0, std::string()},
                             true);
                    }
                }
                round.clear();
                return has_moved;
            }

            /**
             * @brief Connects a worker endpoint to the fabric's master, if one runs.
             * @return False when no master runs or the worker's link is cut.
             * @pre The caller is the pump of a worker endpoint.
             */
            [[nodiscard]] bool connect_to_master()
            {
                std::shared_ptr<Impl> master;
                {
                    std::lock_guard<std::mutex> lock(fabric->mutex);
                    if (worker_index < fabric->is_partitioned.size() &&
                        !fabric->is_partitioned[worker_index])
                        master = fabric->master.lock();
                }
                if (master == nullptr)
                    return false;

                const PeerID peer{next_peer.fetch_add(1)};
                const std::shared_ptr<Link> link = std::make_shared<Link>();
                link->worker_index = worker_index;
                link->ids[index_of(Side::Worker)] = peer;
                link->endpoints[index_of(Side::Worker)] = weak_from_this();
                const std::shared_ptr<Connection> connection =
                    std::make_shared<Connection>(link, peer, control_queue_frames);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    table.emplace(peer.value, connection);
                }
                local_peers.emplace(peer.value, connection);

                if (!master->accept(link))
                {
                    local_peers.erase(peer.value);
                    std::lock_guard<std::mutex> lock(mutex);
                    table.erase(peer.value);
                    return false;
                }
                announce(connection);
                return true;
            }

            /**
             * @brief Makes a worker's due connection attempt and gives up at the deadline.
             * @pre The caller is the pump of a worker endpoint.
             */
            void keep_connected()
            {
                if (!local_peers.empty() || is_abandoned || Clock::now() < next_attempt)
                    return;
                if (connect_to_master())
                {
                    was_connected = true;
                    return;
                }

                const bool has_deadline = worker_options.connect_timeout.count() > 0;
                if (!was_connected && has_deadline &&
                    Clock::now() - started >= worker_options.connect_timeout)
                {
                    abandon("in-process worker " + std::to_string(worker_index) +
                            " found no running master endpoint in " +
                            std::to_string(worker_options.connect_timeout.count()) + " ms");
                    return;
                }
                next_attempt = Clock::now() + worker_options.reconnect_delay;
            }

            /** @brief Returns whether a worker's pump has a connection attempt to wake for. */
            [[nodiscard]] bool awaits_attempt() const
            {
                return side == Side::Worker && local_peers.empty() && !is_abandoned;
            }

            /** @brief Runs the pump: deliver the inbox, move outbound frames, keep connected. */
            void run()
            {
                started = Clock::now();
                next_attempt = started;

                std::deque<Inbound> batch;
                for (;;)
                {
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        const auto is_due = [this] { return !is_accepting || has_work; };
                        if (awaits_attempt())
                            wake_signal.wait_until(lock, next_attempt, is_due);
                        else
                            wake_signal.wait(lock, is_due);
                        if (!is_accepting)
                            return;
                        has_work = false;
                        batch.swap(inbox);
                    }

                    for (Inbound& item : batch)
                    {
                        if (phase.load() != Phase::Running)
                            return;
                        deliver(item);
                    }
                    batch.clear();

                    if (flush_round())
                        wake();
                    if (side == Side::Worker)
                        keep_connected();
                }
            }

            /// @brief The fabric's shared state.
            const std::shared_ptr<InProcessFabric::Impl> fabric;

            /// @brief Which end of its connections the endpoint is.
            const Side side;

            /// @brief The fabric's index of a worker endpoint; 0 on the master.
            const std::size_t worker_index;

            /// @brief The control queue bound of every connection.
            const std::size_t control_queue_frames;

            /// @brief The settings of a worker endpoint.
            const InProcessWorkerOptions worker_options;

            /// @brief Receives the events; set by start().
            TransportEvents* events = nullptr;

            /// @brief Serialises start() and stop().
            std::mutex lifecycle;

            /// @brief Set by the first start(), whatever its outcome.
            bool was_started = false;

            /// @brief Where the endpoint is in its life; send() reads it without a lock.
            std::atomic<Phase> phase{Phase::Idle};

            /// @brief The id the next connection gets; ids start at 1.
            std::atomic<std::uint32_t> next_peer{1};

            /// @brief Payload bytes sent, plus 12 per frame.
            std::atomic<std::uint64_t> bytes_sent{0};

            /// @brief Payload bytes delivered, plus 12 per frame.
            std::atomic<std::uint64_t> bytes_received{0};

            /// @brief Guards inbox, table, has_work and is_accepting.
            std::mutex mutex;

            /// @brief Wakes the pump.
            std::condition_variable wake_signal;

            /// @brief What the other ends sent, in the order it arrived.
            std::deque<Inbound> inbox;

            /// @brief The open connections by PeerID::value; send() looks a peer up here.
            std::unordered_map<std::uint32_t, std::shared_ptr<Connection>> table;

            /// @brief Set when the inbox or an outbox may hold something.
            bool has_work = false;

            /// @brief Set between start() and stop(); the inbox takes entries only then.
            bool is_accepting = false;

            /// @brief Delivers the inbox and drains the outboxes.
            std::thread pump;

            /// @brief The connections whose on_peer_connected() was made; the pump's own.
            std::unordered_map<std::uint32_t, std::shared_ptr<Connection>> local_peers;

            /// @brief The connections of one flush_round(); the pump's own.
            std::vector<std::shared_ptr<Connection>> round;

            /// @brief The frame flush_round() pops into; the pump's own.
            OutboundFrame scratch;

            /// @brief When the pump began; the pump's own.
            Clock::time_point started;

            /// @brief When a worker tries to connect next; the pump's own.
            Clock::time_point next_attempt;

            /// @brief Set once a worker connected; the pump's own.
            bool was_connected = false;

            /// @brief Set once on_connect_abandoned() was made; the pump's own.
            bool is_abandoned = false;
        };

        InProcessFabric::InProcessFabric() : impl_(std::make_shared<Impl>())
        {

        }

        InProcessFabric::~InProcessFabric() = default;

        std::size_t InProcessFabric::worker_count() const
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            return impl_->workers.size();
        }

        void InProcessFabric::set_fault_policy(FaultPolicy policy)
        {
            std::shared_ptr<const FaultPolicy> shared;
            if (policy)
                shared = std::make_shared<const FaultPolicy>(std::move(policy));
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->policy.swap(shared);
        }

        void InProcessFabric::flush_held(std::uint64_t shuffle_seed)
        {
            std::vector<HeldFrame> released;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                released.swap(impl_->held);
            }
            if (shuffle_seed != 0 && released.size() > 1)
            {
                std::mt19937_64 generator(shuffle_seed);
                std::shuffle(released.begin(), released.end(), generator);
            }
            for (HeldFrame& held : released)
            {
                Inbound item;
                item.kind = InboundKind::Frame;
                item.link = held.link;
                item.frame = std::move(held.frame);
                InProcessTransport::Impl::send_to(held.link, held.destination, std::move(item));
            }
        }

        std::size_t InProcessFabric::held_count() const
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            return impl_->held.size();
        }

        void InProcessFabric::set_partitioned(std::size_t worker_index, bool partitioned)
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (worker_index < impl_->is_partitioned.size())
                impl_->is_partitioned[worker_index] = partitioned;
        }

        InProcessTransport::InProcessTransport(std::shared_ptr<Impl> impl) noexcept
            : impl_(std::move(impl))
        {

        }

        std::shared_ptr<InProcessTransport> InProcessTransport::make_master(
            InProcessFabric& fabric, InProcessMasterOptions options)
        {
            std::shared_ptr<Impl> impl = std::make_shared<Impl>(
                fabric.impl_, Side::Master, 0, options.control_queue_frames,
                InProcessWorkerOptions());
            {
                std::lock_guard<std::mutex> lock(fabric.impl_->mutex);
                fabric.impl_->master = impl;
            }
            return std::shared_ptr<InProcessTransport>(new InProcessTransport(std::move(impl)));
        }

        std::shared_ptr<InProcessTransport> InProcessTransport::make_worker(
            InProcessFabric& fabric, InProcessWorkerOptions options)
        {
            std::shared_ptr<Impl> impl;
            {
                std::lock_guard<std::mutex> lock(fabric.impl_->mutex);
                impl = std::make_shared<Impl>(fabric.impl_, Side::Worker,
                                              fabric.impl_->workers.size(),
                                              options.control_queue_frames, options);
                fabric.impl_->workers.push_back(impl);
                fabric.impl_->is_partitioned.push_back(false);
            }
            return std::shared_ptr<InProcessTransport>(new InProcessTransport(std::move(impl)));
        }

        InProcessTransport::~InProcessTransport()
        {
            stop();
        }

        TransportResult InProcessTransport::start(TransportEvents& events)
        {
            Impl& impl = *impl_;
            std::lock_guard<std::mutex> lock(impl.lifecycle);

            TransportResult result;
            if (impl.was_started)
            {
                result.status = TransportStatus::AlreadyStarted;
                result.message = "the in-process endpoint was started before; a transport "
                                 "starts once";
                return result;
            }
            impl.was_started = true;
            if (impl.phase.load() != Impl::Phase::Idle)
            {
                result.status = TransportStatus::Failed;
                result.message = "the in-process endpoint was stopped before it was started";
                return result;
            }

            impl.events = &events;
            {
                std::lock_guard<std::mutex> table_lock(impl.mutex);
                impl.is_accepting = true;
            }
            impl.phase.store(Impl::Phase::Running);
            if (impl.side == Side::Master)
                result.message = K_MASTER_ENDPOINT_NAME;
            try
            {
                impl.pump = std::thread([&impl] { impl.run(); });
            }
            catch (const std::system_error& error)
            {
                impl.phase.store(Impl::Phase::Stopped);
                std::lock_guard<std::mutex> table_lock(impl.mutex);
                impl.is_accepting = false;
                result.status = TransportStatus::Failed;
                result.message = std::string("cannot start the in-process pump thread: ") +
                                 error.what();
            }
            return result;
        }

        void InProcessTransport::stop() noexcept
        {
            Impl& impl = *impl_;
            std::lock_guard<std::mutex> lock(impl.lifecycle);
            if (impl.phase.exchange(Impl::Phase::Stopped) != Impl::Phase::Running)
                return;

            {
                std::lock_guard<std::mutex> table_lock(impl.mutex);
                impl.is_accepting = false;
                impl.inbox.clear();
            }
            impl.wake_signal.notify_all();
            if (impl.pump.joinable())
                impl.pump.join();

            impl.local_peers.clear();
            for (;;)
            {
                std::shared_ptr<Connection> connection;
                {
                    std::lock_guard<std::mutex> table_lock(impl.mutex);
                    if (impl.table.empty())
                        break;
                    connection = std::move(impl.table.begin()->second);
                    impl.table.erase(impl.table.begin());
                }
                connection->outbox.close();
                impl.tell_lost(connection->link, other(impl.side), "the peer stopped");
            }
        }

        SendStatus InProcessTransport::send(PeerID peer, MessageType type,
                                            SushiRuntime::span<const std::byte> payload)
        {
            if (impl_->phase.load(std::memory_order_acquire) != Impl::Phase::Running)
                return SendStatus::Stopped;
            const std::shared_ptr<Connection> connection = impl_->find(peer);
            if (connection == nullptr)
                return SendStatus::UnknownPeer;
            const SendStatus status = connection->outbox.push_control(type, payload);
            if (status == SendStatus::Queued)
                impl_->wake();
            return status;
        }

        SendStatus InProcessTransport::send_stream(PeerID peer,
                                                   std::unique_ptr<FrameSource> source)
        {
            std::shared_ptr<Connection> connection;
            const bool is_running =
                impl_->phase.load(std::memory_order_acquire) == Impl::Phase::Running;
            if (is_running)
                connection = impl_->find(peer);
            if (connection == nullptr)
            {
                source.reset();
                return is_running ? SendStatus::UnknownPeer : SendStatus::Stopped;
            }
            const SendStatus status = connection->outbox.push_stream(std::move(source));
            if (status == SendStatus::Queued)
                impl_->wake();
            return status;
        }

        void InProcessTransport::close_peer(PeerID peer) noexcept
        {
            const std::shared_ptr<Connection> connection = impl_->find(peer);
            if (connection == nullptr)
                return;
            connection->outbox.close();
            impl_->wake();
        }

        TransportExposure InProcessTransport::exposure() const noexcept
        {
            return TransportExposure::Loopback;
        }

        TransportStatistics InProcessTransport::statistics() const noexcept
        {
            TransportStatistics result;
            result.bytes_sent = impl_->bytes_sent.load(std::memory_order_relaxed);
            result.bytes_received = impl_->bytes_received.load(std::memory_order_relaxed);
            return result;
        }

        std::size_t InProcessTransport::worker_index() const noexcept
        {
            return impl_->worker_index;
        }

    } // namespace Distributed
} // namespace SushiRuntime
