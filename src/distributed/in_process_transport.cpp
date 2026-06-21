/**************************************************************************/
/* in_process_transport.cpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

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

        // Upper bound on messages parked by FaultAction::Hold, so a test nemesis
        // that holds aggressively cannot grow memory without bound.
        static constexpr size_t MAX_HELD = 100000;

        // --- endpoint internal state (one per transport) ----------------------
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

            void enqueue(int from_peer, MsgType type, SushiRuntime::span<const std::byte> payload)
            {
                {
                    std::lock_guard<std::mutex> lk(q_mtx);
                    inbox.push(Message{ from_peer, type,
                                        std::vector<std::byte>(payload.begin(), payload.end()) });
                }
                q_cv.notify_one();
            }

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

        // --- shared switchboard ----------------------------------------------
        struct InProcessFabric::Impl
        {
            int worker_count = 0;
            std::mutex mtx;
            std::weak_ptr<InProcessTransport::Impl>              master;
            std::vector<std::weak_ptr<InProcessTransport::Impl>> workers;

            // --- fault injection (no-op unless a policy is installed) ----------
            FaultPolicy fault;

            // Workers currently partitioned off the network (both directions).
            std::vector<bool> partitioned;

            bool is_partitioned(int node_id) const
            {
                return node_id >= 0 && node_id < static_cast<int>(partitioned.size())
                    && partitioned[static_cast<size_t>(node_id)];
            }

            // A message parked by FaultAction::Hold, resolved to its destination so
            // flush_held() can deliver it verbatim regardless of later routing state.
            struct Held
            {
                std::weak_ptr<InProcessTransport::Impl> dst;
                int                    from_peer;
                MsgType                type;
                std::vector<std::byte> payload;
            };
            std::vector<Held> held;

            // Resolves the endpoint for a destination node id (-1 => master).
            std::weak_ptr<InProcessTransport::Impl> endpoint_for(int to_peer) const
            {
                if (to_peer == MASTER_ID)
                    return master;
                if (to_peer >= 0 && to_peer < static_cast<int>(workers.size()))
                    return workers[static_cast<size_t>(to_peer)];
                return {};
            }

            // Applies the fault policy and routes one message to one destination.
            // Caller holds mtx. from_peer/to_peer are node ids (-1 = master).
            void route_locked(int from_peer, int to_peer, MsgType type,
                              SushiRuntime::span<const std::byte> payload)
            {
                // A partitioned worker is cut off in both directions, regardless of
                // any per-message fault policy.
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
                        // Bound the hold buffer: past the cap, a held message is
                        // simply dropped (a link's finite buffer overflowing). The
                        // recovery paths treat it as a normal loss.
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

        InProcessFabric::InProcessFabric(int worker_count)
            : impl_(std::make_shared<Impl>())
        {
            impl_->worker_count = worker_count;
            impl_->workers.resize(static_cast<size_t>(worker_count));
            impl_->partitioned.assign(static_cast<size_t>(worker_count), false);
        }

        InProcessFabric::~InProcessFabric() = default;

        int InProcessFabric::worker_count() const { return impl_->worker_count; }

        void InProcessFabric::set_fault_policy(FaultPolicy policy)
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            impl_->fault = std::move(policy);
        }

        void InProcessFabric::flush_held(uint64_t shuffle_seed)
        {
            std::vector<Impl::Held> drained;
            {
                std::lock_guard<std::mutex> lk(impl_->mtx);
                drained.swap(impl_->held);
            }
            // Optional seeded reordering: deliver the backlog out of order to inject
            // message reordering on top of the delay.
            if (shuffle_seed != 0 && drained.size() > 1)
            {
                std::mt19937_64 rng(shuffle_seed);
                std::shuffle(drained.begin(), drained.end(), rng);
            }
            // Deliver outside the lock: enqueue only touches the destination
            // endpoint's own queue, and we must not hold the fabric mutex while a
            // peer's handler may call back into send().
            for (auto& m : drained)
                if (auto d = m.dst.lock())
                    d->enqueue(m.from_peer, m.type, m.payload);
        }

        std::size_t InProcessFabric::held_count() const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            return impl_->held.size();
        }

        void InProcessFabric::set_partitioned(int worker, bool partitioned)
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            if (worker >= 0 && worker < static_cast<int>(impl_->partitioned.size()))
                impl_->partitioned[static_cast<size_t>(worker)] = partitioned;
        }

        // --- transport endpoint ----------------------------------------------
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

        InProcessTransport::~InProcessTransport()
        {
            stop();
        }

        void InProcessTransport::on_message(MessageHandler handler)
        {
            impl_->handler = std::move(handler);
        }

        void InProcessTransport::start()
        {
            bool expected = false;
            if (!impl_->running.compare_exchange_strong(expected, true))
                return;
            impl_->pump = std::thread([imp = impl_] { imp->pump_loop(); });
        }

        void InProcessTransport::stop()
        {
            bool expected = true;
            if (!impl_->running.compare_exchange_strong(expected, false))
                return;
            impl_->q_cv.notify_all();
            if (impl_->pump.joinable())
                impl_->pump.join();
        }

        void InProcessTransport::send(int peer_id, MsgType type, SushiRuntime::span<const std::byte> payload)
        {
            auto fab = impl_->fabric;
            if (!fab)
                return;

            std::lock_guard<std::mutex> lk(fab->mtx);

            if (impl_->node_id == MASTER_ID)
            {
                // Master -> worker(s). peer_id < 0 means broadcast. Each target is
                // routed through the fault policy independently.
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

        int InProcessTransport::peer_count() const
        {
            auto fab = impl_->fabric;
            if (!fab)
                return 0;
            return impl_->node_id == MASTER_ID ? fab->worker_count : 1;
        }

    } // namespace Distributed
} // namespace SushiRuntime
