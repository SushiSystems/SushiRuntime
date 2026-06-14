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
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            using Clock = std::chrono::steady_clock;
            // After this many failed remote dispatches a node is pinned local so a
            // kernel that always fails on workers cannot ping-pong forever.
            constexpr uint32_t MAX_OFFLOAD_ATTEMPTS = 2;
        }

        struct DistributedCoordinator::Impl : public Core::RefCounted
        {
            sushi_ptr<KernelRegistry>        kernels;
            BufferRegistry*                  buffers = nullptr;
            std::shared_ptr<ITransport>      transport;
            std::shared_ptr<IOffloadPolicy>  policy;

            CompletionHook                   on_complete;
            ResubmitHook                     on_resubmit;

            std::mutex                       mtx;
            std::vector<WorkerStats>         workers;
            // Per-worker bookkeeping kept parallel to `workers` so the policy-facing
            // WorkerStats stays clean: last inbound message time (liveness) and the
            // op set the worker advertised at HELLO (to undo op_support on death).
            std::vector<Clock::time_point>      last_seen;
            std::vector<std::vector<uint32_t>>  worker_ops;
            // op_id -> how many workers expose it; an op is offloadable only when
            // every live worker can run it (the negotiated common kernel set).
            std::unordered_map<uint32_t, int> op_support;

            struct PendingTask { Graph::Node* node = nullptr; int worker = -1; };
            std::unordered_map<uint64_t, PendingTask> pending; // task_id -> {node, worker}
            std::atomic<uint64_t>            next_task_id{1};

            // "ran where" telemetry, so silent local fallback is observable.
            std::atomic<uint64_t>            offloaded_count{0};
            std::atomic<uint64_t>            declined_count{0};
            std::atomic<uint64_t>            redispatched_count{0};

            // --- liveness monitor: declares a silent worker dead and re-injects
            //     its in-flight tasks so the master never stalls on a lost peer --
            //     (atomic: the public setter may race the running monitor thread)
            std::atomic<int64_t>             liveness_timeout_ms{5000};
            std::thread                      monitor_thread;
            std::mutex                       monitor_mtx;
            std::condition_variable          monitor_cv;
            std::atomic<bool>                monitor_running{false};

            void handle_message(int peer, MsgType type, std::span<const std::byte> payload);
            void monitor_loop();
            // Sends a fresh RTT probe to every live worker. Snapshots peer ids
            // under mtx, then sends outside the lock (the reply arrives as PONG).
            void send_rtt_pings();
            // Collects tasks orphaned by a now-dead worker (under mtx); the caller
            // re-injects them via on_resubmit outside the lock.
            std::vector<Graph::Node*> reap_dead_workers();
        };

        void DistributedCoordinator::Impl::handle_message(int peer, MsgType type,
                                                          std::span<const std::byte> payload)
        {
            // Any inbound traffic proves the peer is alive — refresh its liveness
            // before dispatching on type, so heavy workers (busy pulling buffers)
            // are never falsely reaped.
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (peer >= 0 && peer < static_cast<int>(last_seen.size()))
                    last_seen[peer] = Clock::now();
            }

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
                                worker_ops[peer] = ops;
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
                case MsgType::BUFFER_PUSH:
                {
                    // Worker writing an output result back into the master's
                    // authoritative buffer (eager writeback on completion).
                    auto push = BufferPushMsg::deserialize(payload);
                    if (auto e = buffers->lookup_by_id(push.handle.id))
                    {
                        uint64_t n = push.data.size();
                        if (n > e->handle.size_bytes) n = e->handle.size_bytes;
                        std::memcpy(e->ptr, push.data.data(), n);
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
                            node = it->second.node;
                            pending.erase(it);
                        }
                        if (peer >= 0 && peer < static_cast<int>(workers.size()) &&
                            workers[peer].inflight > 0)
                            workers[peer].inflight--;
                    }
                    // A stale result for a task we already re-dispatched (the worker
                    // was slow, not dead) finds nothing in `pending` and is dropped.
                    if (node && on_complete)
                        on_complete(node, res.success);
                    break;
                }
                case MsgType::PONG:
                {
                    // RTT is computed entirely on the master clock: the worker
                    // echoed the steady-clock token we stamped into the PING, so
                    // (now - token) is the round trip free of any clock skew.
                    auto pong = PongMsg::deserialize(payload);
                    auto sent = Clock::time_point(Clock::duration(
                        static_cast<Clock::rep>(pong.token)));
                    auto rtt_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        Clock::now() - sent).count();
                    if (rtt_us < 0) rtt_us = 0;

                    std::lock_guard<std::mutex> lk(mtx);
                    if (peer >= 0 && peer < static_cast<int>(workers.size()))
                    {
                        workers[peer].rtt_micros = static_cast<uint32_t>(
                            rtt_us > UINT32_MAX ? UINT32_MAX : rtt_us);
                        workers[peer].free_mem   = pong.free_mem;
                    }
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
            impl_->last_seen.assign(static_cast<size_t>(n), Clock::now());
            impl_->worker_ops.resize(static_cast<size_t>(n));
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

        void DistributedCoordinator::set_resubmit_hook(ResubmitHook hook)
        {
            impl_->on_resubmit = std::move(hook);
        }

        void DistributedCoordinator::set_liveness_timeout(std::chrono::milliseconds timeout)
        {
            impl_->liveness_timeout_ms.store(timeout.count(), std::memory_order_relaxed);
            // Wake the monitor so it recomputes its poll period against the new
            // timeout instead of finishing a sleep sized to the old (default) one.
            impl_->monitor_cv.notify_all();
        }

        uint64_t DistributedCoordinator::offloaded_count() const
        {
            return impl_->offloaded_count.load(std::memory_order_relaxed);
        }

        uint64_t DistributedCoordinator::declined_count() const
        {
            return impl_->declined_count.load(std::memory_order_relaxed);
        }

        uint64_t DistributedCoordinator::redispatched_count() const
        {
            return impl_->redispatched_count.load(std::memory_order_relaxed);
        }

        int DistributedCoordinator::alive_worker_count() const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            int n = 0;
            for (const auto& w : impl_->workers) if (w.alive) ++n;
            return n;
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

            bool expected = false;
            if (impl_->monitor_running.compare_exchange_strong(expected, true))
                impl_->monitor_thread = std::thread([raw] { raw->monitor_loop(); });
        }

        void DistributedCoordinator::stop()
        {
            if (!impl_)
                return;

            bool expected = true;
            if (impl_->monitor_running.compare_exchange_strong(expected, false))
            {
                impl_->monitor_cv.notify_all();
                if (impl_->monitor_thread.joinable())
                    impl_->monitor_thread.join();
            }

            if (impl_->transport)
                impl_->transport->stop();
        }

        std::vector<Graph::Node*> DistributedCoordinator::Impl::reap_dead_workers()
        {
            std::vector<Graph::Node*> orphans;
            const auto now = Clock::now();
            const auto timeout = std::chrono::milliseconds(
                liveness_timeout_ms.load(std::memory_order_relaxed));

            std::lock_guard<std::mutex> lk(mtx);
            for (size_t i = 0; i < workers.size(); ++i)
            {
                auto& w = workers[i];
                if (!w.alive)
                    continue;
                if (now - last_seen[i] < timeout)
                    continue;

                // Declare the worker dead: drop it from the common kernel set and
                // reset its load so the policy stops choosing it immediately.
                w.alive    = false;
                w.inflight = 0;
                for (uint32_t op : worker_ops[i])
                {
                    auto it = op_support.find(op);
                    if (it != op_support.end() && --it->second <= 0)
                        op_support.erase(it);
                }
                worker_ops[i].clear();
                SR_LOG_WARN("Worker {} missed heartbeats; declaring it dead and "
                            "re-dispatching its tasks.", static_cast<int>(i));

                // Hand back every task still owned by this worker.
                for (auto it = pending.begin(); it != pending.end(); )
                {
                    if (it->second.worker == static_cast<int>(i))
                    {
                        orphans.push_back(it->second.node);
                        it = pending.erase(it);
                    }
                    else ++it;
                }
            }
            return orphans;
        }

        void DistributedCoordinator::Impl::send_rtt_pings()
        {
            std::vector<int> alive_peers;
            {
                std::lock_guard<std::mutex> lk(mtx);
                for (size_t i = 0; i < workers.size(); ++i)
                    if (workers[i].alive)
                        alive_peers.push_back(static_cast<int>(i));
            }
            for (int peer : alive_peers)
            {
                PingMsg ping;
                ping.token = static_cast<uint64_t>(
                    Clock::now().time_since_epoch().count());
                transport->send(peer, MsgType::PING, ping.serialize());
            }
        }

        void DistributedCoordinator::Impl::monitor_loop()
        {
            while (monitor_running.load())
            {
                {
                    std::unique_lock<std::mutex> lk(monitor_mtx);
                    // Poll at a fraction of the timeout so detection latency is
                    // bounded without busy-spinning.
                    auto period = std::chrono::milliseconds(
                        liveness_timeout_ms.load(std::memory_order_relaxed) / 2);
                    if (period.count() < 50) period = std::chrono::milliseconds(50);
                    // Plain wait (no predicate) so set_liveness_timeout()'s notify
                    // wakes us to recompute `period` against the new timeout rather
                    // than sleeping out a stale, longer interval.
                    monitor_cv.wait_for(lk, period);
                }
                if (!monitor_running.load())
                    break;

                // Refresh per-worker RTT / free_mem telemetry the offload policy
                // reasons over, then reap anyone who has gone silent.
                send_rtt_pings();

                auto orphans = reap_dead_workers();
                for (Graph::Node* node : orphans)
                {
                    redispatched_count.fetch_add(1, std::memory_order_relaxed);
                    if (on_resubmit)
                        on_resubmit(node);          // re-offload to a survivor, or run local
                    else if (on_complete)
                        on_complete(node, false);   // no resubmit wired: at least don't stall
                }
            }
        }

        bool DistributedCoordinator::try_offload(Graph::Node* node)
        {
            if (!node) return false;

            // A node that has already failed remotely too many times (dead worker
            // or kernel error) is pinned local: clear its offload flag so this and
            // future dispatch attempts run it on the home machine.
            if (node->offload_attempts_.load(std::memory_order_relaxed) >= MAX_OFFLOAD_ATTEMPTS)
            {
                node->offloadable_ = false;
                impl_->declined_count.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

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
                if (!pick)
                {
                    impl_->declined_count.fetch_add(1, std::memory_order_relaxed);
                    return false; // transparent local fallback
                }
                chosen = *pick;

                msg.task_id = impl_->next_task_id.fetch_add(1, std::memory_order_relaxed);
                impl_->pending[msg.task_id] = Impl::PendingTask{ node, chosen }; // takes over the scheduling ref
                if (chosen >= 0 && chosen < static_cast<int>(impl_->workers.size()))
                    impl_->workers[chosen].inflight++;
            }

            node->offload_attempts_.fetch_add(1, std::memory_order_relaxed);

            auto bytes = msg.serialize();
            impl_->transport->send(chosen, MsgType::TASK_DISPATCH, bytes);
            impl_->offloaded_count.fetch_add(1, std::memory_order_relaxed);
            SR_LOG_INFO("Task {} ({}) offloaded to worker {}.",
                        msg.task_id, node->metadata.name, chosen);
            return true;
        }

    } // namespace Distributed
} // namespace SushiRuntime
