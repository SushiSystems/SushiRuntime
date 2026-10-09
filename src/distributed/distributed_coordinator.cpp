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
 * @brief Dispatches offloadable nodes to workers and re-injects the ones a worker loses.
 * @author Mustafa Garip
 */

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
            /// @brief Caps failed remote dispatches per node (src/distributed/README.md).
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
            /// @brief Holds each worker's last inbound message time (src/distributed/README.md).
            std::vector<Clock::time_point>      last_seen;
            std::vector<std::vector<uint32_t>>  worker_ops;
            /// @brief Counts the workers that expose each op id (src/distributed/README.md).
            std::unordered_map<uint32_t, int> op_support;

            struct PendingTask
            {
                Graph::Node*      node = nullptr;
                int               worker = -1;
                Clock::time_point dispatched_at; // for the per-task deadline
            };
            std::unordered_map<uint64_t, PendingTask> pending; // task_id -> {node, worker}
            std::atomic<uint64_t>            next_task_id{1};

            /// @brief Holds the per-task deadline in ms; 0 disables it (src/distributed/README.md).
            std::atomic<int64_t>             task_deadline_ms{30000};

            // "ran where" telemetry, so silent local fallback is observable.
            std::atomic<uint64_t>            offloaded_count{0};
            std::atomic<uint64_t>            declined_count{0};
            std::atomic<uint64_t>            redispatched_count{0};
            // Writebacks rejected by the version fence (stale/superseded results).
            std::atomic<uint64_t>            stale_writebacks{0};

            /// @brief Holds the ms of silence that mark a worker dead (src/distributed/README.md).
            std::atomic<int64_t>             liveness_timeout_ms{5000};
            std::thread                      monitor_thread;
            std::mutex                       monitor_mtx;
            std::condition_variable          monitor_cv;
            std::atomic<bool>                monitor_running{false};

            /**
             * @brief Handles an incoming message from a worker.
             * Processes different message types such as registration, buffer pushes, and task results.
             * @param peer The integer ID of the worker.
             * @param type The MsgType of the message.
             * @param payload A span of bytes representing the message payload.
             */
            void handle_message(int peer, MsgType type, SushiRuntime::span<const std::byte> payload);

            /**
             * @brief Periodically checks worker liveness and re-dispatches tasks.
             * Runs in a background thread to reap dead workers and timed-out tasks.
             */
            void monitor_loop();

            /**
             * @brief Sends round-trip time ping messages to all alive workers.
             * Sends a fresh RTT probe to every live worker, snapshotting peers under the mutex.
             */
            void send_rtt_pings();

            /**
             * @brief Reaps workers that missed heartbeats.
             * Collects tasks orphaned by a now-dead worker and returns them for re-dispatch.
             * @return A vector of Graph::Node pointers for orphaned tasks.
             */
            std::vector<Graph::Node*> reap_dead_workers();

            /**
             * @brief Reaps tasks that have timed out on alive workers.
             * Collects tasks whose per-task deadline elapsed and returns them for re-dispatch.
             * @return A vector of Graph::Node pointers for timed-out tasks.
             */
            std::vector<Graph::Node*> reap_timed_out_tasks();
        };

        /**
         * @brief Handles an incoming message from a worker.
         * Updates liveness statistics and processes registration, buffers, and results based on the message type.
         * @param peer The integer ID of the worker.
         * @param type The MsgType of the message.
         * @param payload A span of bytes representing the message payload.
         */
        void DistributedCoordinator::Impl::handle_message(int peer, MsgType type,
                                                          SushiRuntime::span<const std::byte> payload)
        {
            // Any inbound message refreshes the peer's liveness (src/distributed/README.md).
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
                    uint32_t ver = r.get<uint32_t>();
                    if (!r.ok() || ver != PROTOCOL_VERSION)
                    {
                        // A protocol mismatch keeps the peer out (src/distributed/README.md).
                        SR_LOG_ERROR("Worker {} HELLO rejected: protocol v{} != v{}.",
                                     peer, ver, PROTOCOL_VERSION);
                        break;
                    }
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
                    // The worker writes an output back to the master (src/distributed/README.md).
                    auto push = BufferPushMsg::deserialize(payload);
                    if (auto e = buffers->lookup_by_id(push.handle.id))
                    {
                        // The version fence drops a stale writeback (src/distributed/README.md).
                        if (push.handle.version < e->handle.version)
                        {
                            stale_writebacks.fetch_add(1, std::memory_order_relaxed);
                            SR_LOG_WARN("Dropping stale writeback for buffer {} "
                                        "(v{} < current v{}).", push.handle.id.value,
                                        push.handle.version, e->handle.version);
                            break;
                        }
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
                    // A result absent from pending is dropped (src/distributed/README.md).
                    if (!node)
                        break;

                    // Output bytes land before successors are released (src/distributed/README.md).
                    if (res.success)
                    {
                        for (size_t i = 0; i < res.outputs.size(); ++i)
                        {
                            const auto& h = res.outputs[i];
                            auto e = buffers->lookup_by_id(h.id);
                            if (!e) continue;
                            if (h.version < e->handle.version)
                            {
                                stale_writebacks.fetch_add(1, std::memory_order_relaxed);
                                continue;
                            }
                            uint64_t n = res.output_data[i].size();
                            if (n > e->handle.size_bytes) n = e->handle.size_bytes;
                            std::memcpy(e->ptr, res.output_data[i].data(), n);
                        }
                    }

                    if (on_complete)
                        on_complete(node, res.success);
                    break;
                }
                case MsgType::PONG:
                {
                    // RTT is measured on the master clock alone (src/distributed/README.md).
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

        /**
         * @brief Constructs a DistributedCoordinator.
         * Initializes internal data structures to manage task offloading and tracking across workers.
         * @param kernels A sushi_ptr to the KernelRegistry.
         * @param buffers A reference to the BufferRegistry.
         * @param transport A std::shared_ptr to the ITransport interface.
         * @param policy A std::shared_ptr to the IOffloadPolicy.
         */
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

        /**
         * @brief Destructs the DistributedCoordinator.
         * Stops operations and background threads before destruction.
         */
        DistributedCoordinator::~DistributedCoordinator()
        {
            stop();
        }

        /**
         * @brief Sets the callback invoked upon task completion.
         * @param hook The CompletionHook to run when a task finishes successfully or fails.
         */
        void DistributedCoordinator::set_completion_hook(CompletionHook hook)
        {
            impl_->on_complete = std::move(hook);
        }

        /**
         * @brief Sets the callback invoked to resubmit a failed or orphaned task.
         * @param hook The ResubmitHook to run when a task needs to be re-dispatched.
         */
        void DistributedCoordinator::set_resubmit_hook(ResubmitHook hook)
        {
            impl_->on_resubmit = std::move(hook);
        }

        /**
         * @brief Updates the maximum time without heartbeats before declaring a worker dead.
         * Wakes the monitor thread to apply the new timeout immediately.
         * @param timeout A std::chrono::milliseconds specifying the new timeout.
         */
        void DistributedCoordinator::set_liveness_timeout(std::chrono::milliseconds timeout)
        {
            impl_->liveness_timeout_ms.store(timeout.count(), std::memory_order_relaxed);
            // The notify makes the monitor recompute its poll period (src/distributed/README.md).
            impl_->monitor_cv.notify_all();
        }

        /**
         * @brief Updates the maximum time a task can remain pending on a worker.
         * Wakes the monitor thread to apply the new deadline immediately.
         * @param deadline A std::chrono::milliseconds specifying the new per-task deadline.
         */
        void DistributedCoordinator::set_task_deadline(std::chrono::milliseconds deadline)
        {
            impl_->task_deadline_ms.store(deadline.count(), std::memory_order_relaxed);
            // The notify makes the monitor recompute its poll period (src/distributed/README.md).
            impl_->monitor_cv.notify_all();
        }

        /**
         * @brief Retrieves the total number of offloaded tasks.
         * @return An uint64_t representing the offloaded task count.
         */
        uint64_t DistributedCoordinator::offloaded_count() const
        {
            return impl_->offloaded_count.load(std::memory_order_relaxed);
        }

        /**
         * @brief Retrieves the total number of declined tasks.
         * @return An uint64_t representing the declined task count.
         */
        uint64_t DistributedCoordinator::declined_count() const
        {
            return impl_->declined_count.load(std::memory_order_relaxed);
        }

        /**
         * @brief Retrieves the total number of re-dispatched tasks.
         * @return An uint64_t representing the re-dispatched task count.
         */
        uint64_t DistributedCoordinator::redispatched_count() const
        {
            return impl_->redispatched_count.load(std::memory_order_relaxed);
        }

        /**
         * @brief Retrieves the total number of stale writebacks dropped.
         * @return An uint64_t representing the stale writeback count.
         */
        uint64_t DistributedCoordinator::stale_writeback_count() const
        {
            return impl_->stale_writebacks.load(std::memory_order_relaxed);
        }

        /**
         * @brief Counts the currently alive workers in the network.
         * Iterates over all known workers and sums those marked as alive.
         * @return An integer count of alive workers.
         */
        int DistributedCoordinator::alive_worker_count() const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            int n = 0;
            for (const auto& w : impl_->workers) if (w.alive) ++n;
            return n;
        }

        /**
         * @brief Checks if a specific operation is supported by the alive workers.
         * @param op The Graph::OpID representing the operation.
         * @return A boolean which is true if at least one worker supports it, false otherwise.
         */
        bool DistributedCoordinator::supports(Graph::OpID op) const
        {
            std::lock_guard<std::mutex> lk(impl_->mtx);
            auto it = impl_->op_support.find(op.value);
            return it != impl_->op_support.end() && it->second > 0;
        }

        /**
         * @brief Advances the version of the buffer registered at an address.
         * @param usm_ptr An address with no registered buffer is ignored.
         */
        void DistributedCoordinator::bump_buffer_version(void* usm_ptr)
        {
            impl_->buffers->bump_version(usm_ptr);
        }

        /**
         * @brief Starts the coordinator and its monitor thread.
         * Begins listening for transport messages and sets up liveness monitoring.
         */
        void DistributedCoordinator::start()
        {
            Impl* raw = impl_.get();
            impl_->transport->on_message(
                [raw](int peer, MsgType type, SushiRuntime::span<const std::byte> payload)
                {
                    raw->handle_message(peer, type, payload);
                });
            impl_->transport->start();

            bool expected = false;
            if (impl_->monitor_running.compare_exchange_strong(expected, true))
                impl_->monitor_thread = std::thread([raw] { raw->monitor_loop(); });
        }

        /**
         * @brief Stops the coordinator and waits for threads to exit.
         * Signals the monitor to shutdown and stops the transport interface.
         */
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

        /**
         * @brief Reaps workers that missed heartbeats.
         * Collects tasks orphaned by dead workers and drops them from the common kernel set.
         * @return A vector of Graph::Node pointers for orphaned tasks.
         */
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

                // A dead worker leaves the kernel set with no load (src/distributed/README.md).
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

        /**
         * @brief Reaps tasks that have timed out on alive workers.
         * Collects tasks whose per-task deadline elapsed indicating a lost dispatch/result.
         * @return A vector of Graph::Node pointers for timed-out tasks.
         */
        std::vector<Graph::Node*> DistributedCoordinator::Impl::reap_timed_out_tasks()
        {
            std::vector<Graph::Node*> orphans;
            const int64_t deadline_ms = task_deadline_ms.load(std::memory_order_relaxed);
            if (deadline_ms <= 0)
                return orphans; // per-task deadline disabled

            const auto now = Clock::now();
            const auto deadline = std::chrono::milliseconds(deadline_ms);

            std::lock_guard<std::mutex> lk(mtx);
            for (auto it = pending.begin(); it != pending.end(); )
            {
                if (now - it->second.dispatched_at < deadline)
                {
                    ++it;
                    continue;
                }

                // A task past its deadline counts as failed (src/distributed/README.md).
                const int w = it->second.worker;
                if (w >= 0 && w < static_cast<int>(workers.size()) && workers[w].inflight > 0)
                    workers[w].inflight--;
                SR_LOG_WARN("Task {} exceeded its {}ms deadline on worker {}; "
                            "re-dispatching.", it->first, deadline_ms, w);
                orphans.push_back(it->second.node);
                it = pending.erase(it);
            }
            return orphans;
        }

        /**
         * @brief Sends round-trip time ping messages to all alive workers.
         * Broadcasts a steady-clock token to measure latency across the network.
         */
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

        /**
         * @brief Periodically checks worker liveness and re-dispatches tasks.
         * Wakes at fractions of the tightest deadline to reap timed out operations efficiently.
         */
        void DistributedCoordinator::Impl::monitor_loop()
        {
            while (monitor_running.load())
            {
                {
                    std::unique_lock<std::mutex> lk(monitor_mtx);
                    // Half the tightest deadline, with a 50 ms floor (src/distributed/README.md).
                    int64_t live_ms = liveness_timeout_ms.load(std::memory_order_relaxed);
                    int64_t task_ms = task_deadline_ms.load(std::memory_order_relaxed);
                    int64_t tightest = live_ms;
                    if (task_ms > 0 && task_ms < tightest) tightest = task_ms;
                    auto period = std::chrono::milliseconds(tightest / 2);
                    if (period.count() < 50) period = std::chrono::milliseconds(50);
                    // No predicate, so a setter's notify ends the wait (src/distributed/README.md).
                    monitor_cv.wait_for(lk, period);
                }
                if (!monitor_running.load())
                    break;

                // Pings refresh the RTT and free_mem the policy reads (src/distributed/README.md).
                send_rtt_pings();

                // Dead workers and overdue tasks re-inject alike (src/distributed/README.md).
                auto orphans = reap_dead_workers();
                auto stuck   = reap_timed_out_tasks();
                orphans.insert(orphans.end(), stuck.begin(), stuck.end());
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

        /**
         * @brief Attempts to offload a graph node to a suitable worker.
         * Delegates selection to the policy and performs buffer tracking for the offload.
         * @param node A pointer to the Graph::Node to offload.
         * @return A boolean which is true if the node was successfully dispatched, false otherwise.
         */
        bool DistributedCoordinator::try_offload(Graph::Node* node)
        {
            if (!node) return false;

            // offload() allocates nothing for a flagged node (src/distributed/README.md).
            Graph::OffloadState& off = node->offload();

            // A node at the attempt cap is pinned local (src/distributed/README.md).
            if (off.attempts.load(std::memory_order_relaxed) >= MAX_OFFLOAD_ATTEMPTS)
            {
                off.offloadable = false;
                impl_->declined_count.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            // Input versions come from the master registry (src/distributed/README.md).
            TaskDispatchMsg msg;
            msg.op_id = off.remote_op;
            msg.meta  = node->metadata;

            // A double-buffered node takes handles from live pointers (src/distributed/README.md).
            std::vector<BufferHandle> in_handles;
            std::vector<BufferHandle> out_handles;
            const bool dynamic_io = static_cast<bool>(off.live_reads) ||
                                    static_cast<bool>(off.live_writes);
            if (dynamic_io)
            {
                if (off.live_reads)
                    for (void* p : *off.live_reads)
                    {
                        auto h = impl_->buffers->lookup(p);
                        if (!h) { impl_->declined_count.fetch_add(1, std::memory_order_relaxed); return false; }
                        in_handles.push_back(*h);
                    }
                if (off.live_writes)
                    for (void* p : *off.live_writes)
                    {
                        auto h = impl_->buffers->lookup(p);
                        if (!h) { impl_->declined_count.fetch_add(1, std::memory_order_relaxed); return false; }
                        out_handles.push_back(*h);
                    }
            }
            else
            {
                in_handles  = off.in_handles;
                out_handles = off.out_handles;
            }

            uint64_t est_bytes = 0;
            for (const auto& h : in_handles)
            {
                BufferHandle cur = h;
                if (auto e = impl_->buffers->lookup_by_id(h.id)) cur = e->handle;
                msg.inputs.push_back(cur);
                est_bytes += cur.size_bytes;
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
                impl_->pending[msg.task_id] =
                    Impl::PendingTask{ node, chosen, Clock::now() }; // takes over the scheduling ref
                if (chosen >= 0 && chosen < static_cast<int>(impl_->workers.size()))
                    impl_->workers[chosen].inflight++;
            }

            // Outputs get a fresh version once a worker is chosen (src/distributed/README.md).
            for (const auto& h : out_handles)
            {
                impl_->buffers->bump_version_by_id(h.id);
                BufferHandle cur = h;
                if (auto e = impl_->buffers->lookup_by_id(h.id)) cur = e->handle;
                msg.outputs.push_back(cur);
            }

            off.attempts.fetch_add(1, std::memory_order_relaxed);

            auto bytes = msg.serialize();
            impl_->transport->send(chosen, MsgType::TASK_DISPATCH, bytes);
            impl_->offloaded_count.fetch_add(1, std::memory_order_relaxed);
            SR_LOG_INFO("Task {} ({}) offloaded to worker {}.",
                        msg.task_id, node->metadata.name, chosen);
            return true;
        }

    } // namespace Distributed
} // namespace SushiRuntime
