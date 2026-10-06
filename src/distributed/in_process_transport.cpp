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
 * @brief Routes messages between a master and workers inside one process, with fault injection.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/in_process_transport.hpp>

#include <queue>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <condition_variable>

namespace SushiRuntime
{
    namespace Distributed
    {
        // Master is modelled as node_id == -1; workers are 0..N-1.
        static constexpr int MASTER_ID = -1;

        /// @brief Caps the messages FaultAction::Hold may park (src/distributed/README.md).
        static constexpr size_t MAX_HELD = 100000;

        /// @brief Holds one endpoint's inbox and the thread that pumps it.
        struct InProcessTransport::Impl
        {
            struct Message
            {
                int                    from_peer;
                MsgType                type;
                std::vector<std::byte> payload;
            };

            std::shared_ptr<InProcessFabric::Impl> fabric;
            int                                    node_id = MASTER_ID;
            MessageHandler                         handler;

            std::mutex                 q_mtx;
            std::condition_variable    q_cv;
            std::queue<Message>        inbox;
            std::thread                pump;
            std::atomic<bool>          running{false};

            /**
             * @brief Enqueues a message for processing by this endpoint.
             * Pushes the message payload into the inbox and notifies the pump thread.
             * @param from_peer The integer ID of the sending peer.
             * @param type The MsgType of the message.
             * @param payload A span of bytes containing the message payload.
             */
            void enqueue(int from_peer, MsgType type, SushiRuntime::span<const std::byte> payload)
            {
                {
                    std::lock_guard<std::mutex> lk(q_mtx);
                    inbox.push(Message{ from_peer, type,
                                        std::vector<std::byte>(payload.begin(), payload.end()) });
                }
                q_cv.notify_one();
            }

            /**
             * @brief Continuously dequeues and dispatches incoming messages.
             * Blocks until messages arrive or the transport stops, passing them to the registered handler.
             */
            void pump_loop()
            {
                for (;;)
                {
                    Message msg;
                    {
                        std::unique_lock<std::mutex> lk(q_mtx);
                        q_cv.wait(lk, [&] { return !inbox.empty() || !running.load(); });
                        if (inbox.empty() && !running.load())
                            return;
                        msg = std::move(inbox.front());
                        inbox.pop();
                    }
                    if (handler)
                        handler(msg.from_peer, msg.type, msg.payload);
                }
            }
        };

        /// @brief Holds the switchboard that routes messages between endpoints.
        struct InProcessFabric::Impl
        {
            int worker_count = 0;
            std::mutex mtx;
            std::weak_ptr<InProcessTransport::Impl>              master;
            std::vector<std::weak_ptr<InProcessTransport::Impl>> workers;

            /// @brief Holds the installed fault policy; an empty one delivers every message.
            FaultPolicy fault;

            // Workers currently partitioned off the network (both directions).
            std::vector<bool> partitioned;

            /**
             * @brief Checks if a specific node is partitioned from the network.
             * Determines whether the node is cut off based on the fabric's partition configuration.
             * @param node_id The integer ID of the node to check.
             * @return A boolean which is true if the node is partitioned, false otherwise.
             */
            bool is_partitioned(int node_id) const
            {
                return node_id >= 0 && node_id < static_cast<int>(partitioned.size())
                    && partitioned[static_cast<size_t>(node_id)];
            }

            /// @brief Holds a parked message and its resolved endpoint (src/distributed/README.md).
            struct Held
            {
                std::weak_ptr<InProcessTransport::Impl> dst;
                int                    from_peer;
                MsgType                type;
                std::vector<std::byte> payload;
            };
            std::vector<Held> held;

            /**
             * @brief Resolves the endpoint for a destination node ID.
             * Retrieves a weak pointer to the target's transport implementation.
             * @param to_peer The integer ID of the destination peer (-1 for master).
             * @return A std::weak_ptr to the destination's InProcessTransport::Impl.
             */
            std::weak_ptr<InProcessTransport::Impl> endpoint_for(int to_peer) const
            {
                if (to_peer == MASTER_ID)
                    return master;
                if (to_peer >= 0 && to_peer < static_cast<int>(workers.size()))
                    return workers[static_cast<size_t>(to_peer)];
                return {};
            }

            /**
             * @brief Applies the fault policy and routes one message to one destination.
             * Drops, holds, or delivers the message based on the active policy. Caller must hold the mutex.
             * @param from_peer The integer ID of the sending peer.
             * @param to_peer The integer ID of the receiving peer.
             * @param type The MsgType of the message to route.
             * @param payload A span of bytes containing the message payload.
             */
            void route_locked(int from_peer, int to_peer, MsgType type,
                              SushiRuntime::span<const std::byte> payload)
            {
                // A partition cuts a worker off in both directions, before any fault policy.
                if (is_partitioned(from_peer) || is_partitioned(to_peer))
                    return;

                auto dst = endpoint_for(to_peer);
                FaultAction action = fault
                    ? fault(from_peer, to_peer, type) : FaultAction::Deliver;

                switch (action)
                {
                    case FaultAction::Drop:
                        return;
                    case FaultAction::Hold:
                        // Past MAX_HELD a held message is dropped (src/distributed/README.md).
                        if (held.size() < MAX_HELD)
                            held.push_back(Held{ dst, from_peer, type,
                                std::vector<std::byte>(payload.begin(), payload.end()) });
                        return;
                    case FaultAction::Duplicate:
                        if (auto d = dst.lock()) d->enqueue(from_peer, type, payload);
                        [[fallthrough]];
                    case FaultAction::Deliver:
                        if (auto d = dst.lock()) d->enqueue(from_peer, type, payload);
                        return;
                }
            }
        };

        /**
         * @brief Constructs an InProcessFabric for simulating distributed communication.
         * Initializes the central switchboard with the specified number of workers.
         * @param worker_count The total number of workers in the fabric.
         */
        InProcessFabric::InProcessFabric(int worker_count)
            : impl_(std::make_shared<Impl>())
        {
            impl_->worker_count = worker_count;
            impl_->workers.resize(static_cast<size_t>(worker_count));
            impl_->partitioned.assign(static_cast<size_t>(worker_count), false);
        }

        /**
         * @brief Destructs the InProcessFabric.
         */
        InProcessFabric::~InProcessFabric() = default;

        /**
         * @brief Retrieves the total number of workers configured in the fabric.
         * @return An integer representing the worker count.
         */
        int InProcessFabric::worker_count() const { return impl_->worker_count; }

        /**
         * @brief Installs a fault policy for the fabric.
         * Enables fault injection by intercepting messages and applying drop, hold, or deliver actions.
         * @param policy The FaultPolicy to apply.
         */
        void InProcessFabric::set_fault_policy(FaultPolicy policy)
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            impl_->fault = std::move(policy);
        }

        /**
         * @brief Flushes all held messages, optionally shuffling their order.
         * Delivers any messages parked by a FaultAction::Hold, enabling out-of-order delivery simulation.
         * @param shuffle_seed The seed for the random number generator, or 0 to preserve order.
         */
        void InProcessFabric::flush_held(uint64_t shuffle_seed)
        {
            std::vector<Impl::Held> drained;
            {
                std::lock_guard<std::mutex> lk(impl_->mtx);
                drained.swap(impl_->held);
            }
            // A non-zero seed delivers the backlog out of order (src/distributed/README.md).
            if (shuffle_seed != 0 && drained.size() > 1)
            {
                std::mt19937_64 rng(shuffle_seed);
                std::shuffle(drained.begin(), drained.end(), rng);
            }
            // Delivery runs outside the fabric mutex (src/distributed/README.md).
            for (auto& m : drained)
                if (auto d = m.dst.lock())
                    d->enqueue(m.from_peer, m.type, m.payload);
        }

        /**
         * @brief Retrieves the count of currently held messages.
         * @return A std::size_t representing the number of messages held by the fault policy.
         */
        std::size_t InProcessFabric::held_count() const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            return impl_->held.size();
        }

        /**
         * @brief Sets or clears a network partition for a specific worker.
         * Isolates the worker in both directions when partitioned is true.
         * @param worker The integer ID of the worker to partition.
         * @param partitioned A boolean indicating whether to partition (true) or heal (false).
         */
        void InProcessFabric::set_partitioned(int worker, bool partitioned)
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            if (worker >= 0 && worker < static_cast<int>(impl_->partitioned.size()))
                impl_->partitioned[static_cast<size_t>(worker)] = partitioned;
        }

        /**
         * @brief Constructs an InProcessTransport and registers it with the fabric.
         * Attaches this endpoint to the central switchboard for routing.
         * @param fabric The InProcessFabric to register with.
         * @param node_id The integer ID of this node (-1 for master).
         */
        InProcessTransport::InProcessTransport(InProcessFabric& fabric, int node_id)
            : impl_(std::make_shared<Impl>())
        {
            impl_->fabric  = fabric.impl_;
            impl_->node_id = node_id;

            std::lock_guard<std::mutex> lk(fabric.impl_->mtx);
            if (node_id == MASTER_ID)
                fabric.impl_->master = impl_;
            else if (node_id >= 0 && node_id < static_cast<int>(fabric.impl_->workers.size()))
                fabric.impl_->workers[static_cast<size_t>(node_id)] = impl_;
        }

        /**
         * @brief Destructs the InProcessTransport.
         * Stops the transport threads before destruction.
         */
        InProcessTransport::~InProcessTransport()
        {
            stop();
        }

        /**
         * @brief Registers a callback to handle incoming messages.
         * @param handler The MessageHandler to invoke when a message arrives.
         */
        void InProcessTransport::on_message(MessageHandler handler)
        {
            impl_->handler = std::move(handler);
        }

        /**
         * @brief Starts the transport.
         * Initiates the pump loop thread to process the inbox.
         */
        void InProcessTransport::start()
        {
            bool expected = false;
            if (!impl_->running.compare_exchange_strong(expected, true))
                return;
            impl_->pump = std::thread([imp = impl_] { imp->pump_loop(); });
        }

        /**
         * @brief Stops the transport.
         * Signals the pump loop to exit and joins its thread.
         */
        void InProcessTransport::stop()
        {
            bool expected = true;
            if (!impl_->running.compare_exchange_strong(expected, false))
                return;
            impl_->q_cv.notify_all();
            if (impl_->pump.joinable())
                impl_->pump.join();
        }

        /**
         * @brief Sends a message to a specific peer through the fabric.
         * Broadcasts to all peers if peer_id is negative.
         * @param peer_id The integer ID of the destination peer, or negative for broadcast.
         * @param type The MsgType to send.
         * @param payload A span of bytes containing the message payload.
         */
        void InProcessTransport::send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload)
        {
            auto fab = impl_->fabric;
            if (!fab)
                return;

            std::lock_guard<std::mutex> lk(fab->mtx);

            if (impl_->node_id == MASTER_ID)
            {
                // A negative peer_id broadcasts; each target passes the fault policy alone.
                if (peer_id < 0)
                {
                    for (size_t i = 0; i < fab->workers.size(); ++i)
                        fab->route_locked(MASTER_ID, static_cast<int>(i), type, payload);
                }
                else if (peer_id < static_cast<int>(fab->workers.size()))
                {
                    fab->route_locked(MASTER_ID, peer_id, type, payload);
                }
            }
            else
            {
                // Worker -> master. The receiver learns which worker sent it.
                fab->route_locked(impl_->node_id, MASTER_ID, type, payload);
            }
        }

        /**
         * @brief Retrieves the number of connected peers.
         * @return An integer representing the number of peers (worker count for master, 1 for worker).
         */
        int InProcessTransport::peer_count() const
        {
            auto fab = impl_->fabric;
            if (!fab)
                return 0;
            return impl_->node_id == MASTER_ID ? fab->worker_count : 1;
        }

    } // namespace Distributed
} // namespace SushiRuntime
