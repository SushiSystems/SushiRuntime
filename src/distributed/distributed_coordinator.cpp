/**************************************************************************/
/* distributed_coordinator.cpp                                            */
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

            struct PendingTask
            {
                Graph::Node*      node = nullptr;
                int               worker = -1;
                Clock::time_point dispatched_at; // for the per-task deadline
            };
            std::unordered_map<uint64_t, PendingTask> pending; // task_id -> {node, worker}
            std::atomic<uint64_t>            next_task_id{1};

            // Per-task deadline: a dispatched task must produce a result within this
            // bound or it is treated as failed and re-dispatched. This is a SECOND
            // liveness axis, independent of heartbeats: a worker can keep
            // heartbeating yet a single lost TASK_DISPATCH or TASK_RESULT would
            // otherwise leave the task pending forever. 0 disables it.
            std::atomic<int64_t>             task_deadline_ms{30000};

            // "ran where" telemetry, so silent local fallback is observable.
            std::atomic<uint64_t>            offloaded_count{0};
            std::atomic<uint64_t>            declined_count{0};
            std::atomic<uint64_t>            redispatched_count{0};
            // Writebacks rejected by the version fence (stale/superseded results).
            std::atomic<uint64_t>            stale_writebacks{0};

            // --- liveness monitor: declares a silent worker dead and re-injects
            //     its in-flight tasks so the master never stalls on a lost peer --
            //     (atomic: the public setter may race the running monitor thread)
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
                    uint32_t ver = r.get<uint32_t>();
                    if (!r.ok() || ver != PROTOCOL_VERSION)
                    {
                        // A peer speaking a different protocol must not join: its
                        // raw structs would be parsed against an incompatible layout.
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
                    // Worker writing an output result back into the master's
                    // authoritative buffer (eager writeback on completion).
                    auto push = BufferPushMsg::deserialize(payload);
                    if (auto e = buffers->lookup_by_id(push.handle.id))
                    {
                        // Version fence (Kleppmann fencing token): each (re)dispatch
                        // bumps the output version, so a writeback carrying a version
                        // older than the buffer's current one belongs to a superseded
                        // dispatch — e.g. a worker we declared dead that was only slow.
                        // Accepting it would clobber a newer result, so we drop it.
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
                    // A stale result for a task we already re-dispatched (the worker
                    // was slow, not dead) finds nothing in `pending` and is dropped —
                    // both its completion AND its now-superseded output bytes.
                    if (!node)
                        break;

                    // Apply the inline output bytes to the master's authoritative
                    // buffers BEFORE releasing successors, so a dependent reader sees
                    // the produced data. Version-fenced: a result stamped older than
                    // the buffer's current version is a superseded run and ignored.
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
            // Wake the monitor so it recomputes its poll period against the new
            // timeout instead of finishing a sleep sized to the old (default) one.
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
            // Wake the monitor so it recomputes its poll period against the new
            // deadline rather than sleeping out a stale, longer interval.
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

                // The task has been in flight too long even though its worker may
                // still be heartbeating — a lost dispatch or lost result. Treat it
                // as failed: drop the worker's inflight count and re-inject the node.
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
                    // Poll at a fraction of the tightest deadline (worker liveness or
                    // per-task) so detection latency is bounded without busy-spinning.
                    int64_t live_ms = liveness_timeout_ms.load(std::memory_order_relaxed);
                    int64_t task_ms = task_deadline_ms.load(std::memory_order_relaxed);
                    int64_t tightest = live_ms;
                    if (task_ms > 0 && task_ms < tightest) tightest = task_ms;
                    auto period = std::chrono::milliseconds(tightest / 2);
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

                // Two recovery axes: workers gone silent (heartbeat), and individual
                // tasks stuck past their deadline on a still-live worker (lost
                // dispatch/result). Both re-inject the orphaned node the same way.
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

            // A double-buffered node carries no baked handles; resolve them from its
            // live pointer lists for this step. Any pointer that is not (still)
            // registered makes the task fall back to local execution.
            std::vector<BufferHandle> in_handles;
            std::vector<BufferHandle> out_handles;
            const bool dynamic_io = static_cast<bool>(node->live_reads_) ||
                                    static_cast<bool>(node->live_writes_);
            if (dynamic_io)
            {
                if (node->live_reads_)
                    for (void* p : *node->live_reads_)
                    {
                        auto h = impl_->buffers->lookup(p);
                        if (!h) { impl_->declined_count.fetch_add(1, std::memory_order_relaxed); return false; }
                        in_handles.push_back(*h);
                    }
                if (node->live_writes_)
                    for (void* p : *node->live_writes_)
                    {
                        auto h = impl_->buffers->lookup(p);
                        if (!h) { impl_->declined_count.fetch_add(1, std::memory_order_relaxed); return false; }
                        out_handles.push_back(*h);
                    }
            }
            else
            {
                in_handles  = node->in_handles_;
                out_handles = node->out_handles_;
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

            // Only now that the task is committed to a worker, stamp its outputs
            // with a fresh fencing token. Done after the policy decision so a
            // declined (locally-run) task never bumps a version spuriously. Each
            // (re)dispatch bumps again, so the master's BUFFER_PUSH handler rejects
            // any writeback carrying an older version — the result of a superseded
            // (slow/zombie) run that would otherwise clobber fresh data.
            for (const auto& h : out_handles)
            {
                impl_->buffers->bump_version_by_id(h.id);
                BufferHandle cur = h;
                if (auto e = impl_->buffers->lookup_by_id(h.id)) cur = e->handle;
                msg.outputs.push_back(cur);
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
