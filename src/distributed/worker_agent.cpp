/**************************************************************************/
/* worker_agent.cpp                                                       */
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

#include <SushiRuntime/distributed/worker_agent.hpp>

#include <deque>
#include <queue>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        struct WorkerAgent::Impl : public Core::RefCounted
        {
            Execution::RuntimeContext&   ctx;
            std::shared_ptr<ITransport>  transport;
            sushi_ptr<KernelRegistry>    kernels;

            // --- buffer cache, keyed by id; pull-on-miss against the master ---
            struct CacheEntry { uint32_t version = 0; void* ptr = nullptr; uint64_t size = 0; };
            std::mutex                              cache_mtx;
            std::condition_variable                 cache_cv;
            std::unordered_map<uint64_t, CacheEntry> cache;

            // --- dispatch jobs run on a dedicated thread (NOT the pump thread),
            //     so a blocking buffer pull never starves message delivery ------
            std::mutex                    job_mtx;
            std::condition_variable       job_cv;
            std::queue<TaskDispatchMsg>   jobs;
            std::thread                   exec_thread;
            std::atomic<bool>             running{false};

            // Backpressure: cap the pending-job queue so a master that floods this
            // worker faster than it drains cannot grow memory without bound. When
            // full, a dispatch is fast-failed (TASK_RESULT{false}) so the master
            // re-dispatches it elsewhere or runs it locally, rather than silently
            // queueing unbounded work.
            std::atomic<size_t>           max_jobs{1024};

            // --- idempotency: a bounded set of recently-seen task ids so a
            //     duplicated TASK_DISPATCH (transport retransmit) never runs the
            //     kernel twice. Re-dispatches after failure use a fresh task_id, so
            //     they are correctly NOT deduplicated. -------------------------
            static constexpr size_t       MAX_SEEN_IDS = 4096;
            std::mutex                    seen_mtx;
            std::unordered_set<uint64_t>  seen_ids;
            std::deque<uint64_t>          seen_order;

            /**
             * @brief Records a task ID to detect duplicate dispatches.
             * Returns false if the task ID was already seen and processed.
             * @param task_id The uint64_t identifier of the task.
             * @return A boolean which is true if the task ID is new, false otherwise.
             */
            bool mark_seen(uint64_t task_id)
            {
                std::lock_guard<std::mutex> lk(seen_mtx);
                if (!seen_ids.insert(task_id).second)
                    return false;
                seen_order.push_back(task_id);
                if (seen_order.size() > MAX_SEEN_IDS)
                {
                    seen_ids.erase(seen_order.front());
                    seen_order.pop_front();
                }
                return true;
            }

            // Bound on how long resolve_input() waits for a pulled buffer before
            // giving up, so a lost BUFFER_PUSH fails the task instead of hanging it.
            std::chrono::milliseconds     pull_timeout{5000};

            // Set once the master acknowledges our HELLO. Until then the heartbeat
            // loop keeps re-sending HELLO, so a lost registration message (a real
            // possibility under packet loss) does not strand the worker forever.
            std::atomic<bool>             registered{false};

            /**
             * @brief Sends a registration HELLO message to the master.
             * Announces the worker's protocol version and supported operations.
             */
            void send_hello()
            {
                ByteWriter w;
                w.put<uint32_t>(PROTOCOL_VERSION);
                auto ops = kernels->known_ops();
                w.put<uint32_t>(static_cast<uint32_t>(ops.size()));
                for (auto op : ops) w.put<uint32_t>(op.value);
                transport->send(0, MsgType::REGISTER_HELLO, w.view());
            }

            // --- liveness: a periodic HEARTBEAT so the master can detect a dead
            //     or hung worker and re-dispatch its tasks ----------------------
            std::chrono::milliseconds     heartbeat_interval{1000};
            std::thread                   heartbeat_thread;
            std::mutex                    heartbeat_mtx;
            std::condition_variable       heartbeat_cv;

            Impl(Execution::RuntimeContext& c) : ctx(c) {}

            /**
             * @brief Periodically sends heartbeats to the master to prove liveness.
             */
            void heartbeat_loop();

            /**
             * @brief Handles incoming messages from the transport.
             * @param peer The integer ID of the peer.
             * @param type The MsgType of the message.
             * @param payload A span of bytes representing the message payload.
             */
            void on_message(int peer, MsgType type, SushiRuntime::span<const std::byte> payload);

            /**
             * @brief Continuously dequeues and executes dispatch jobs.
             */
            void exec_loop();

            /**
             * @brief Executes a single dispatched task.
             * Resolves buffers, invokes the kernel, and sends the result.
             * @param msg The TaskDispatchMsg containing the task parameters.
             */
            void execute_dispatch(const TaskDispatchMsg& msg);

            /**
             * @brief Calculates the current device-memory headroom.
             * Prefers the Intel free-memory query, falling back to total global memory.
             * @return An uint64_t representing the available memory in bytes.
             */
            uint64_t device_free_mem() const;

            /**
             * @brief Retrieves a local USM pointer for a required input buffer.
             * Pulls data from the master if the local cache is absent or stale.
             * @param h The BufferHandle describing the required buffer.
             * @return A void pointer to the memory, or nullptr on timeout.
             */
            void* resolve_input(const BufferHandle& h);

            /**
             * @brief Prepares a local USM buffer for output.
             * Allocates a new buffer or reuses an appropriately sized existing one.
             * @param h The BufferHandle describing the output buffer.
             * @return A void pointer to the allocated memory.
             */
            void* resolve_output(const BufferHandle& h);

            /**
             * @brief Frees all cached USM allocations.
             */
            void free_all();
        };

        /**
         * @brief Retrieves a local USM pointer for a required input buffer.
         * Pulls data from the master if the local cache is absent or stale.
         * @param h The BufferHandle describing the required buffer.
         * @return A void pointer to the memory, or nullptr on timeout.
         */
        void* WorkerAgent::Impl::resolve_input(const BufferHandle& h)
        {
            std::unique_lock<std::mutex> lk(cache_mtx);

            auto fresh = [&]() -> void*
            {
                auto it = cache.find(h.id.value);
                if (it != cache.end() && it->second.ptr && it->second.version >= h.version)
                    return it->second.ptr;
                return nullptr;
            };

            if (void* p = fresh()) return p;

            // Miss / stale -> request the bytes and wait for the BUFFER_PUSH the
            // pump thread will deposit into the cache.
            lk.unlock();
            BufferRequestMsg req{ h.id, h.version };
            transport->send(0, MsgType::BUFFER_REQUEST, req.serialize());
            lk.lock();

            // Bounded wait: if the push never arrives (lost message, dead master),
            // return nullptr so execute_dispatch fails the task cleanly instead of
            // blocking the exec thread forever. A non-positive timeout waits forever.
            auto ready = [&] { return fresh() != nullptr || !running.load(); };
            if (pull_timeout.count() > 0)
                cache_cv.wait_for(lk, pull_timeout, ready);
            else
                cache_cv.wait(lk, ready);
            return fresh();
        }

        /**
         * @brief Prepares a local USM buffer for output.
         * Allocates a new buffer or reuses an appropriately sized existing one.
         * @param h The BufferHandle describing the output buffer.
         * @return A void pointer to the allocated memory.
         */
        void* WorkerAgent::Impl::resolve_output(const BufferHandle& h)
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            auto& e = cache[h.id.value];
            if (!e.ptr || e.size != h.size_bytes)
            {
                if (e.ptr) sycl::free(e.ptr, ctx.get_queue(0));
                e.ptr  = sycl::malloc_shared(h.size_bytes, ctx.get_queue(0));
                e.size = h.size_bytes;
            }
            e.version = h.version;
            return e.ptr;
        }

        /**
         * @brief Handles incoming messages from the transport.
         * Processes registration acks, task dispatches, pings, and buffer pushes.
         * @param peer The integer ID of the peer.
         * @param type The MsgType of the message.
         * @param payload A span of bytes representing the message payload.
         */
        void WorkerAgent::Impl::on_message(int /*peer*/, MsgType type, SushiRuntime::span<const std::byte> payload)
        {
            switch (type)
            {
                case MsgType::REGISTER_ACK:
                    if (!registered.exchange(true))
                        SR_LOG_INFO("Worker agent registered with master.");
                    break;

                case MsgType::TASK_DISPATCH:
                {
                    auto msg = TaskDispatchMsg::deserialize(payload);
                    // Drop a duplicate dispatch so the kernel runs at most once per
                    // task id; the original is already queued or done.
                    if (!mark_seen(msg.task_id))
                    {
                        SR_LOG_WARN("Worker ignoring duplicate dispatch of task {}.", msg.task_id);
                        break;
                    }
                    const uint64_t tid = msg.task_id;
                    bool queued = false;
                    {
                        std::lock_guard<std::mutex> lk(job_mtx);
                        const size_t cap = max_jobs.load(std::memory_order_relaxed);
                        if (cap == 0 || jobs.size() < cap) // 0 == unbounded
                        {
                            jobs.push(std::move(msg));
                            queued = true;
                        }
                    }
                    if (queued)
                    {
                        job_cv.notify_one();
                    }
                    else
                    {
                        // Overloaded: fast-fail so the master recovers the task.
                        SR_LOG_WARN("Worker job queue full; rejecting task {}.", tid);
                        TaskResultMsg r;
                        r.task_id = tid;
                        r.success = false;
                        transport->send(0, MsgType::TASK_RESULT, r.serialize());
                    }
                    break;
                }
                case MsgType::PING:
                {
                    // Echo the master's token back and piggyback our free-memory
                    // headroom so the master can refresh both RTT and free_mem.
                    auto ping = PingMsg::deserialize(payload);
                    PongMsg pong;
                    pong.token    = ping.token;
                    pong.free_mem = device_free_mem();
                    transport->send(0, MsgType::PONG, pong.serialize());
                    break;
                }
                case MsgType::BUFFER_PUSH:
                {
                    auto push = BufferPushMsg::deserialize(payload);
                    {
                        std::lock_guard<std::mutex> lk(cache_mtx);
                        auto& e = cache[push.handle.id.value];
                        // Ignore a stale/duplicate push that would downgrade a cache
                        // entry we already hold at an equal-or-newer version.
                        if (e.ptr && push.handle.version < e.version)
                            break;
                        if (!e.ptr || e.size != push.handle.size_bytes)
                        {
                            if (e.ptr) sycl::free(e.ptr, ctx.get_queue(0));
                            e.ptr  = sycl::malloc_shared(push.handle.size_bytes, ctx.get_queue(0));
                            e.size = push.handle.size_bytes;
                        }
                        std::memcpy(e.ptr, push.data.data(), push.data.size());
                        e.version = push.handle.version;
                    }
                    cache_cv.notify_all();
                    break;
                }
                default:
                    break;
            }
        }

        /**
         * @brief Executes a single dispatched task.
         * Resolves buffers, invokes the kernel via TaskGraph, and sends the result.
         * @param msg The TaskDispatchMsg containing the task parameters.
         */
        void WorkerAgent::Impl::execute_dispatch(const TaskDispatchMsg& msg)
        {
            TaskResultMsg result;
            result.task_id = msg.task_id;
            result.success = true;

            const auto* kernel = kernels->find(msg.op_id);
            if (!kernel)
            {
                SR_LOG_ERROR("Worker received unknown OpID {}.", msg.op_id.value);
                result.success = false;
                transport->send(0, MsgType::TASK_RESULT, result.serialize());
                return;
            }

            std::vector<void*> in_ptrs, out_ptrs;
            in_ptrs.reserve(msg.inputs.size());
            out_ptrs.reserve(msg.outputs.size());

            // Resolve inputs first; a pull that timed out yields nullptr. Running the
            // kernel on a null buffer would crash the worker, so fail the task
            // instead and let the master re-dispatch it to a healthy peer.
            bool inputs_ok = true;
            for (const auto& h : msg.inputs)
            {
                void* p = resolve_input(h);
                if (!p) inputs_ok = false;
                in_ptrs.push_back(p);
            }
            if (!inputs_ok)
            {
                SR_LOG_ERROR("Worker could not resolve inputs for task {} "
                             "(buffer pull timed out); failing it.", msg.task_id);
                result.success = false;
                transport->send(0, MsgType::TASK_RESULT, result.serialize());
                return;
            }

            for (const auto& h : msg.outputs) out_ptrs.push_back(resolve_output(h));

            try
            {
                // Run the kernel through the worker's OWN TaskGraph + scheduler
                // rather than inline on queue(0): the node carries its input/output
                // pointers, so the scheduler routes it to the queue that owns the USM
                // (correct on a multi-device worker) and the work participates in the
                // worker's normal NUMA-aware execution. The remote kernel is a host-
                // work shape (queue + deps -> event), so it drops straight into a
                // host-fallback task node.
                Graph::TaskGraph g(ctx);
                const auto* k = kernel;
                const auto  meta = msg.meta;
                g.add_task(msg.meta, in_ptrs, out_ptrs,
                    [k, in_ptrs, out_ptrs, meta](sycl::queue& q,
                        const std::vector<sycl::event>&) -> sycl::event
                    {
                        return (*k)(q, in_ptrs, out_ptrs, meta);
                    });
                ctx.get_execution_engine().execute(g.compile());

                // Carry each output's bytes inline in the result, so completion and
                // data reach the master atomically — a lost result re-dispatches the
                // whole task rather than leaving the master "done" with no data.
                result.outputs = msg.outputs; // new versions now live on this worker
                result.output_data.resize(msg.outputs.size());
                {
                    std::lock_guard<std::mutex> lk(cache_mtx);
                    for (size_t i = 0; i < msg.outputs.size(); ++i)
                    {
                        auto it = cache.find(msg.outputs[i].id.value);
                        if (it != cache.end() && it->second.ptr)
                            result.output_data[i].assign(
                                static_cast<std::byte*>(it->second.ptr),
                                static_cast<std::byte*>(it->second.ptr) + it->second.size);
                    }
                }
            }
            catch (...)
            {
                SR_LOG_ERROR("Worker kernel for OpID {} threw.", msg.op_id.value);
                result.success = false;
                result.outputs.clear();
                result.output_data.clear();
            }

            transport->send(0, MsgType::TASK_RESULT, result.serialize());
        }

        /**
         * @brief Calculates the current device-memory headroom.
         * Prefers the Intel free-memory query, falling back to total global memory.
         * @return An uint64_t representing the available memory in bytes.
         */
        uint64_t WorkerAgent::Impl::device_free_mem() const
        {
            try
            {
                auto dev = ctx.get_queue(0).get_device();
                // free_memory landed at version 5 of the Intel device-info
                // extension; guard on the exact feature-test value so the symbol
                // is only referenced where it is guaranteed to exist.
#if defined(SYCL_EXT_INTEL_DEVICE_INFO) && SYCL_EXT_INTEL_DEVICE_INFO >= 5
                if (dev.has(sycl::aspect::ext_intel_free_memory))
                    return dev.get_info<sycl::ext::intel::info::device::free_memory>();
#endif
                // No free-memory query on this device: fall back to total capacity
                // as a coarse headroom proxy (better than reporting "unknown").
                return dev.get_info<sycl::info::device::global_mem_size>();
            }
            catch (...)
            {
                return 0; // unknown -> the policy treats 0 as "no headroom info".
            }
        }

        /**
         * @brief Periodically sends heartbeats to the master to prove liveness.
         */
        void WorkerAgent::Impl::heartbeat_loop()
        {
            while (running.load())
            {
                std::unique_lock<std::mutex> lk(heartbeat_mtx);
                heartbeat_cv.wait_for(lk, heartbeat_interval, [&] { return !running.load(); });
                if (!running.load()) return;
                lk.unlock();
                // Keep retrying registration until the master acknowledges it.
                if (!registered.load(std::memory_order_relaxed))
                    send_hello();
                transport->send(0, MsgType::HEARTBEAT, {});
            }
        }

        /**
         * @brief Continuously dequeues and executes dispatch jobs.
         */
        void WorkerAgent::Impl::exec_loop()
        {
            for (;;)
            {
                TaskDispatchMsg job;
                {
                    std::unique_lock<std::mutex> lk(job_mtx);
                    job_cv.wait(lk, [&] { return !jobs.empty() || !running.load(); });
                    if (jobs.empty() && !running.load()) return;
                    job = std::move(jobs.front());
                    jobs.pop();
                }
                execute_dispatch(job);
            }
        }

        /**
         * @brief Frees all cached USM allocations.
         */
        void WorkerAgent::Impl::free_all()
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            for (auto& [id, e] : cache)
                if (e.ptr) sycl::free(e.ptr, ctx.get_queue(0));
            cache.clear();
        }

        // --- public wrapper ---------------------------------------------------
        /**
         * @brief Constructs a WorkerAgent instance.
         * Initializes the worker with a runtime context, transport layer, and kernel registry.
         * @param ctx A reference to the Execution::RuntimeContext.
         * @param transport A std::shared_ptr to the ITransport interface.
         * @param kernels A sushi_ptr to the KernelRegistry.
         */
        WorkerAgent::WorkerAgent(Execution::RuntimeContext& ctx,
                                 std::shared_ptr<ITransport> transport,
                                 sushi_ptr<KernelRegistry>   kernels)
            : impl_(make_sushi<Impl>(ctx))
        {
            impl_->transport = std::move(transport);
            impl_->kernels   = std::move(kernels);
        }

        /**
         * @brief Destructs the WorkerAgent.
         * Automatically stops threads and releases resources.
         */
        WorkerAgent::~WorkerAgent()
        {
            stop();
        }

        /**
         * @brief Configures the frequency of liveness heartbeats sent to the master.
         * @param interval A std::chrono::milliseconds specifying the duration between heartbeats.
         */
        void WorkerAgent::set_heartbeat_interval(std::chrono::milliseconds interval)
        {
            impl_->heartbeat_interval = interval;
        }

        /**
         * @brief Sets the maximum time to wait for buffer pulls from the master.
         * @param timeout A std::chrono::milliseconds specifying the timeout duration.
         */
        void WorkerAgent::set_pull_timeout(std::chrono::milliseconds timeout)
        {
            impl_->pull_timeout = timeout;
        }

        /**
         * @brief Sets the maximum number of pending jobs in the worker queue.
         * Dispatches exceeding this limit are fast-failed.
         * @param max_jobs A size_t representing the maximum queue size.
         */
        void WorkerAgent::set_max_jobs(size_t max_jobs)
        {
            impl_->max_jobs.store(max_jobs, std::memory_order_relaxed);
        }

        /**
         * @brief Starts the worker agent threads and network handlers.
         * Begins listening for messages and kicks off the background execution and heartbeat loops.
         */
        void WorkerAgent::start()
        {
            bool expected = false;
            if (!impl_->running.compare_exchange_strong(expected, true))
                return;

            Impl* raw = impl_.get();
            impl_->transport->on_message(
                [raw](int peer, MsgType type, SushiRuntime::span<const std::byte> payload)
                {
                    raw->on_message(peer, type, payload);
                });
            impl_->transport->start();

            // Announce our protocol version + kernels. The heartbeat loop re-sends
            // this until REGISTER_ACK, so a lost HELLO does not strand the worker.
            impl_->send_hello();

            impl_->exec_thread = std::thread([raw] { raw->exec_loop(); });

            // A zero interval disables heartbeats (tests use this to feign death).
            if (impl_->heartbeat_interval.count() > 0)
                impl_->heartbeat_thread = std::thread([raw] { raw->heartbeat_loop(); });
        }

        /**
         * @brief Blocks the calling thread until the agent stops.
         */
        void WorkerAgent::run()
        {
            // Park until stop() flips running; exec/pump threads do the work.
            std::unique_lock<std::mutex> lk(impl_->job_mtx);
            impl_->job_cv.wait(lk, [this] { return !impl_->running.load(); });
        }

        /**
         * @brief Stops the worker agent and cleans up resources.
         * Signals background threads to terminate and frees cached USM buffers.
         */
        void WorkerAgent::stop()
        {
            bool expected = true;
            if (!impl_->running.compare_exchange_strong(expected, false))
                return;

            impl_->job_cv.notify_all();
            impl_->cache_cv.notify_all();
            impl_->heartbeat_cv.notify_all();
            if (impl_->heartbeat_thread.joinable())
                impl_->heartbeat_thread.join();
            if (impl_->exec_thread.joinable())
                impl_->exec_thread.join();

            if (impl_->transport)
                impl_->transport->stop();

            impl_->free_all();
        }

    } // namespace Distributed
} // namespace SushiRuntime
