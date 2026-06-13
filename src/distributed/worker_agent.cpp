/**************************************************************************/
/* worker_agent.cpp                                                       */
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

#include <SushiRuntime/distributed/worker_agent.hpp>

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <condition_variable>
#include <unordered_map>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
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

            Impl(Execution::RuntimeContext& c) : ctx(c) {}

            void on_message(int peer, MsgType type, std::span<const std::byte> payload);
            void exec_loop();
            void execute_dispatch(const TaskDispatchMsg& msg);

            // Returns a local USM pointer holding {id} at >= the required version,
            // pulling it from the master if the cache misses or is stale.
            void* resolve_input(const BufferHandle& h);
            // Allocates (or reuses) a local USM buffer for an output handle.
            void* resolve_output(const BufferHandle& h);

            void free_all();
        };

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

            cache_cv.wait(lk, [&] { return fresh() != nullptr || !running.load(); });
            return fresh();
        }

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

        void WorkerAgent::Impl::on_message(int /*peer*/, MsgType type, std::span<const std::byte> payload)
        {
            switch (type)
            {
                case MsgType::REGISTER_ACK:
                    SR_LOG_INFO("Worker agent registered with master.");
                    break;

                case MsgType::TASK_DISPATCH:
                {
                    auto msg = TaskDispatchMsg::deserialize(payload);
                    {
                        std::lock_guard<std::mutex> lk(job_mtx);
                        jobs.push(std::move(msg));
                    }
                    job_cv.notify_one();
                    break;
                }
                case MsgType::BUFFER_PUSH:
                {
                    auto push = BufferPushMsg::deserialize(payload);
                    {
                        std::lock_guard<std::mutex> lk(cache_mtx);
                        auto& e = cache[push.handle.id.value];
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

            for (const auto& h : msg.inputs)  in_ptrs.push_back(resolve_input(h));
            for (const auto& h : msg.outputs) out_ptrs.push_back(resolve_output(h));

            try
            {
                sycl::event e = (*kernel)(ctx.get_queue(0), in_ptrs, out_ptrs, msg.meta);
                e.wait();
                result.outputs = msg.outputs; // new versions now live on this worker
            }
            catch (...)
            {
                SR_LOG_ERROR("Worker kernel for OpID {} threw.", msg.op_id.value);
                result.success = false;
            }

            transport->send(0, MsgType::TASK_RESULT, result.serialize());
        }

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

        void WorkerAgent::Impl::free_all()
        {
            std::lock_guard<std::mutex> lk(cache_mtx);
            for (auto& [id, e] : cache)
                if (e.ptr) sycl::free(e.ptr, ctx.get_queue(0));
            cache.clear();
        }

        // --- public wrapper ---------------------------------------------------
        WorkerAgent::WorkerAgent(Execution::RuntimeContext& ctx,
                                 std::shared_ptr<ITransport> transport,
                                 sushi_ptr<KernelRegistry>   kernels)
            : impl_(make_sushi<Impl>(ctx))
        {
            impl_->transport = std::move(transport);
            impl_->kernels   = std::move(kernels);
        }

        WorkerAgent::~WorkerAgent()
        {
            stop();
        }

        void WorkerAgent::start()
        {
            bool expected = false;
            if (!impl_->running.compare_exchange_strong(expected, true))
                return;

            Impl* raw = impl_.get();
            impl_->transport->on_message(
                [raw](int peer, MsgType type, std::span<const std::byte> payload)
                {
                    raw->on_message(peer, type, payload);
                });
            impl_->transport->start();

            // Announce ourselves and the kernels this binary can run.
            ByteWriter w;
            auto ops = impl_->kernels->known_ops();
            w.put<uint32_t>(static_cast<uint32_t>(ops.size()));
            for (auto op : ops) w.put<uint32_t>(op.value);
            impl_->transport->send(0, MsgType::REGISTER_HELLO, w.view());

            impl_->exec_thread = std::thread([raw] { raw->exec_loop(); });
        }

        void WorkerAgent::run()
        {
            // Park until stop() flips running; exec/pump threads do the work.
            std::unique_lock<std::mutex> lk(impl_->job_mtx);
            impl_->job_cv.wait(lk, [this] { return !impl_->running.load(); });
        }

        void WorkerAgent::stop()
        {
            bool expected = true;
            if (!impl_->running.compare_exchange_strong(expected, false))
                return;

            impl_->job_cv.notify_all();
            impl_->cache_cv.notify_all();
            if (impl_->exec_thread.joinable())
                impl_->exec_thread.join();

            if (impl_->transport)
                impl_->transport->stop();

            impl_->free_all();
        }

    } // namespace Distributed
} // namespace SushiRuntime
