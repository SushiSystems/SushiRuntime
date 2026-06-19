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
#include <thread>
#include <vector>
#include <atomic>
#include <condition_variable>

namespace SushiRuntime
{
    namespace Distributed
    {
        // Master is modelled as node_id == -1; workers are 0..N-1.
        static constexpr int MASTER_ID = -1;

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
        };

        InProcessFabric::InProcessFabric(int worker_count)
            : impl_(std::make_shared<Impl>())
        {
            impl_->worker_count = worker_count;
            impl_->workers.resize(static_cast<size_t>(worker_count));
        }

        InProcessFabric::~InProcessFabric() = default;

        int InProcessFabric::worker_count() const { return impl_->worker_count; }

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
                // Master -> worker(s). peer_id < 0 means broadcast.
                auto deliver = [&](size_t idx)
                {
                    if (auto dst = fab->workers[idx].lock())
                        dst->enqueue(MASTER_ID, type, payload);
                };

                if (peer_id < 0)
                {
                    for (size_t i = 0; i < fab->workers.size(); ++i) deliver(i);
                }
                else if (peer_id < static_cast<int>(fab->workers.size()))
                {
                    deliver(static_cast<size_t>(peer_id));
                }
            }
            else
            {
                // Worker -> master. The receiver learns which worker sent it.
                if (auto dst = fab->master.lock())
                    dst->enqueue(impl_->node_id, type, payload);
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
