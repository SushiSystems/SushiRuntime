/**************************************************************************/
/* distributed_coordinator.cpp                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#include <SushiRuntime/distributed/distributed_coordinator.hpp>

#include <mutex>
#include <vector>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        struct DistributedCoordinator::Impl : public Core::RefCounted
        {
            sushi_ptr<KernelRegistry>        kernels;
            BufferRegistry*                  buffers = nullptr;
            std::shared_ptr<ITransport>      transport;
            std::shared_ptr<IOffloadPolicy>  policy;

            CompletionHook                   on_complete;

            std::mutex                       mtx;
            std::vector<WorkerStats>         workers;
            // op_id -> how many workers expose it; an op is offloadable only when
            // every live worker can run it (the negotiated common kernel set).
            std::unordered_map<uint32_t, int> op_support;

            std::unordered_map<uint64_t, Graph::Node*> pending; // task_id -> node
            std::atomic<uint64_t>            next_task_id{1};

            void handle_message(int peer, MsgType type, std::span<const std::byte> payload);
        };

        void DistributedCoordinator::Impl::handle_message(int peer, MsgType type,
                                                          std::span<const std::byte> payload)
        {
            switch (type)
            {
                case MsgType::REGISTER_HELLO:
                {
                    ByteReader r(payload);
                    uint32_t n = r.get<uint32_t>();
                    std::vector<uint32_t> ops;
                    for (uint32_t i = 0; i < n && r.ok(); ++i) ops.push_back(r.get<uint32_t>());

                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        if (peer >= 0 && peer < static_cast<int>(workers.size()))
                        {
                            auto& w = workers[peer];
                            if (!w.alive)
                            {
                                w.alive = true;
                                for (uint32_t op : ops) op_support[op]++;
                            }
                        }
                    }
                    transport->send(peer, MsgType::REGISTER_ACK, {});
                    SR_LOG_INFO("Worker {} registered with {} kernels.", peer, n);
                    break;
                }
                case MsgType::BUFFER_REQUEST:
                {
                    auto req = BufferRequestMsg::deserialize(payload);
                    if (auto e = buffers->lookup_by_id(req.id))
                    {
                        BufferPushMsg push;
                        push.handle = e->handle;
                        push.data.resize(e->handle.size_bytes);
                        std::memcpy(push.data.data(), e->ptr, e->handle.size_bytes);
                        auto bytes = push.serialize();
                        transport->send(peer, MsgType::BUFFER_PUSH, bytes);
                    }
                    break;
                }
                case MsgType::TASK_RESULT:
                {
                    auto res = TaskResultMsg::deserialize(payload);
                    Graph::Node* node = nullptr;
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        if (auto it = pending.find(res.task_id); it != pending.end())
                        {
                            node = it->second;
                            pending.erase(it);
                        }
                        if (peer >= 0 && peer < static_cast<int>(workers.size()) &&
                            workers[peer].inflight > 0)
                            workers[peer].inflight--;
                    }
                    if (node && on_complete)
                        on_complete(node, res.success);
                    break;
                }
                case MsgType::HEARTBEAT:
                default:
                    break;
            }
        }

        DistributedCoordinator::DistributedCoordinator(sushi_ptr<KernelRegistry> kernels,
                                                       BufferRegistry&           buffers,
                                                       std::shared_ptr<ITransport>     transport,
                                                       std::shared_ptr<IOffloadPolicy> policy)
            : impl_(make_sushi<Impl>())
        {
            impl_->kernels   = std::move(kernels);
            impl_->buffers   = &buffers;
            impl_->transport = std::move(transport);
            impl_->policy    = std::move(policy);

            const int n = impl_->transport->peer_count();
            impl_->workers.resize(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
                impl_->workers[i].worker_id = i;
        }

        DistributedCoordinator::~DistributedCoordinator()
        {
            stop();
        }

        void DistributedCoordinator::set_completion_hook(CompletionHook hook)
        {
            impl_->on_complete = std::move(hook);
        }

        bool DistributedCoordinator::supports(Graph::OpID op) const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            auto it = impl_->op_support.find(op.value);
            return it != impl_->op_support.end() && it->second > 0;
        }

        void DistributedCoordinator::start()
        {
            Impl* raw = impl_.get();
            impl_->transport->on_message(
                [raw](int peer, MsgType type, std::span<const std::byte> payload)
                {
                    raw->handle_message(peer, type, payload);
                });
            impl_->transport->start();
        }

        void DistributedCoordinator::stop()
        {
            if (impl_ && impl_->transport)
                impl_->transport->stop();
        }

        bool DistributedCoordinator::try_offload(Graph::Node* node)
        {
            if (!node) return false;

            // Refresh input handle versions: the master is authoritative, so a
            // buffer written since compile() carries a newer version, which is
            // exactly what tells a worker its cached copy is stale.
            TaskDispatchMsg msg;
            msg.op_id = node->remote_op_;
            msg.meta  = node->metadata;

            uint64_t est_bytes = 0;
            for (const auto& h : node->in_handles_)
            {
                BufferHandle cur = h;
                if (auto e = impl_->buffers->lookup_by_id(h.id)) cur = e->handle;
                msg.inputs.push_back(cur);
                est_bytes += cur.size_bytes;
            }
            for (const auto& h : node->out_handles_)
            {
                BufferHandle cur = h;
                if (auto e = impl_->buffers->lookup_by_id(h.id)) cur = e->handle;
                msg.outputs.push_back(cur);
            }

            int chosen = -1;
            {
                std::lock_guard<std::mutex> lk(impl_->mtx);
                auto pick = impl_->policy->pick_worker(*node, impl_->workers, est_bytes);
                if (!pick) return false; // transparent local fallback
                chosen = *pick;

                msg.task_id = impl_->next_task_id.fetch_add(1, std::memory_order_relaxed);
                impl_->pending[msg.task_id] = node; // takes over the scheduling ref
                if (chosen >= 0 && chosen < static_cast<int>(impl_->workers.size()))
                    impl_->workers[chosen].inflight++;
            }

            auto bytes = msg.serialize();
            impl_->transport->send(chosen, MsgType::TASK_DISPATCH, bytes);
            return true;
        }

    } // namespace Distributed
} // namespace SushiRuntime
