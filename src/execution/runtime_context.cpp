/**************************************************************************/
/* runtime_context.cpp                                                    */
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

#include <map>
#include <array>
#include <mutex>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <SushiRuntime/core/logger.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#ifdef SUSHI_DISTRIBUTED
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#endif

namespace SushiRuntime
{
    namespace Execution
    {
        using namespace SushiRuntime::Topology;
        using namespace SushiRuntime::Core;

        /**
         * @brief Private implementation of RuntimeContext (Pimpl).
         * 
         * Handles hardware topology, queues, and allocators.
         */
        struct RuntimeContext::Impl : public Core::RefCounted
        {
            /** @brief Injected topology source (discovery + NUMA mapping). [DIP] */
            std::shared_ptr<Topology::ITopologyProvider> provider;

            /** @brief Injected NUMA distance (SLIT) model. [DIP] */
            std::shared_ptr<Topology::INumaDistanceModel> distance_model;

            /** @brief Hardware inventory. */
            std::vector<HardwareNode> nodes;

            /** @brief Cached logical-core -> NUMA node map (from the provider). */
            std::vector<int> core_to_numa;
            
            /** @brief Device discovery units: NUMA ID -> queue list. */
            std::map<int, std::vector<sycl::queue>> numa_aware_queues;
            
            /** @brief Flat list for fast access to all queues. */
            std::vector<sycl::queue> all_queues;

            /**
             * @brief USM pointer -> owning queue index (into all_queues).
             *
             * Populated by RuntimeContext::malloc_* so the scheduler can run a
             * task on the exact queue/context its data was allocated on. Pointer
             * introspection (get_pointer_type/device) is unreliable for picking
             * an owner when one platform's USM is visible (as a different kind)
             * in sibling contexts, so this authoritative map is the source of
             * truth; introspection remains only as a best-effort fallback.
             */
            std::mutex usm_owner_mtx;
            std::unordered_map<void*, size_t> usm_owner;

            /** @brief NUMA-local allocators using atomic storage. */
            std::atomic<SushiRuntime::Memory::USMAllocator*> numa_allocators[Core::MAX_NUMA_NODES];

            /**
             * @brief Execution engine (owns persistent TaskScheduler).
             *
             * Stored as a raw pointer and initialised lazily after probe_hardware()
             * so that the engine can receive a fully-initialised queue list.
             * Lifetime is always shorter than the Impl itself — shutdown() is called
             * in ~Impl before any other resource is released. [Madde 18 fix]
             */
            std::unique_ptr<ExecutionEngine> engine;

#ifdef SUSHI_DISTRIBUTED
            /** @brief Deployment role; Standalone unless explicitly promoted. */
            RuntimeContext::RuntimeRole role = RuntimeContext::RuntimeRole::Standalone;
            /** @brief Master-side buffer id registry (created in Master mode). */
            std::unique_ptr<Distributed::BufferRegistry> buffers;
            /** @brief Master-side offload coordinator (created in Master mode). */
            std::unique_ptr<Distributed::DistributedCoordinator> coordinator;
#endif

            Impl()
            {
                for (size_t i = 0; i < Core::MAX_NUMA_NODES; ++i)
                    numa_allocators[i].store(nullptr, std::memory_order_relaxed);
            }

            ~Impl() 
            {
                // --------------------------------------------------------
                // Destruction order is critical for correctness. [Madde 18]
                //
                //  1. Shut down the ExecutionEngine first. This joins all
                //     worker threads, eliminating any dangling references
                //     to queues, allocators, or other Impl members.
                //
                //  2. Drain remaining GPU work via wait_and_throw() so that
                //     all hardware operations complete before SYCL objects
                //     go out of scope.
                //
                //  3. Release NUMA allocators.
                // --------------------------------------------------------

#ifdef SUSHI_DISTRIBUTED
                // Stop the network first so no late remote completion can touch the
                // scheduler while/after it is being torn down.
                if (coordinator)
                {
                    coordinator->stop();
                    coordinator.reset();
                }
#endif

                if (engine)
                {
                    engine->shutdown();
                    engine.reset();
                }

                for (auto& q : all_queues) 
                {
                    q.wait_and_throw();
                }

                /** @brief Cleanup atomic pointers manually. */
                for (size_t i = 0; i < Core::MAX_NUMA_NODES; ++i)
                {
                    auto* ptr = numa_allocators[i].load(std::memory_order_relaxed);
                    if (ptr) 
                    {
                        ptr->release();
                    }
                }
            }
        };

        RuntimeContext::RuntimeContext(const Topology::HardwareFilterConfig& config,
                                       std::shared_ptr<Topology::ITopologyProvider> provider,
                                       std::shared_ptr<Topology::INumaDistanceModel> distance_model)
            : impl_(make_sushi<Impl>())
        {
            SR_LOG_INFO("Initializing compute stack...");

            // Fall back to the real hwloc-backed defaults when nothing is injected.
            impl_->provider = provider ? std::move(provider)
                                       : std::make_shared<Topology::HwlocTopologyProvider>();
            impl_->distance_model = distance_model ? std::move(distance_model)
                                                   : std::make_shared<Topology::HwlocDistanceModel>();

            // Cache the CPU->NUMA map once; the scheduler reads it to build
            // hierarchical steal victim orders.
            impl_->core_to_numa = impl_->provider->cpu_core_to_numa();

            probe_hardware(config);

            // Construct the ExecutionEngine after hardware is probed so the engine
            // receives a fully-initialised context with all queues available.
            // [Madde 7] Persistent scheduler is started here and lives for the
            // entire lifetime of the RuntimeContext.
            impl_->engine = std::make_unique<ExecutionEngine>(*this);
        }

        RuntimeContext::~RuntimeContext() = default;

        /**
         * @brief Hardware discovery and topology mapping process.
         */
        void RuntimeContext::probe_hardware(const Topology::HardwareFilterConfig& config) 
        {
            try 
            {
                /** @brief Step 1+2: discovery + NUMA mapping via the injected provider. */
                impl_->nodes = impl_->provider->enumerate(config);

                /** @brief Step 3: queue configuration - create SYCL queues. */
                impl_->all_queues.clear();
                impl_->numa_aware_queues.clear();

                for (const auto& node : impl_->nodes) 
                {
                    sycl::property_list props;
                    if (!node.sycl_dev.is_cpu() && !node.sycl_dev.is_gpu()) 
                    {
                        // FPGAs or custom accelerators often execute better in-order or don't support full OOO queues natively.
                        props = { sycl::property::queue::in_order() };
                    }
                    
                    sycl::queue q(node.sycl_dev, [](sycl::exception_list elist) 
                    {
                        for (auto e : elist) std::rethrow_exception(e);
                    }, props);

                    impl_->all_queues.push_back(q);

                    int effective_numa = node.numa_node_id;
                    if (effective_numa < 0 || static_cast<size_t>(effective_numa) >= Core::MAX_NUMA_NODES)
                    {
                        SR_LOG_WARN("Device {} reported invalid NUMA {}. Falling back to NUMA 0 for queue mapping.",
                                    node.name,
                                    node.numa_node_id);
                        effective_numa = 0;
                    }

                    impl_->numa_aware_queues[effective_numa].push_back(q);

                    SR_LOG_INFO("Device: {} | NUMA: {} | Memory: {} MB | Compute units: {}",
                               node.name,
                               effective_numa,
                               node.global_mem_size / (1024 * 1024),
                               node.max_compute_units);
                }

                SR_LOG_INFO("Infrastructure ready. Total devices: {}, NUMA domains: {}", 
                            impl_->all_queues.size(), 
                            impl_->numa_aware_queues.size());

                get_allocator(0);
            }
            catch (const std::exception& e) 
            {
                SR_LOG_ERROR("Critical initialization failure: {}", e.what());
                throw;
            }
        }

        sycl::queue& RuntimeContext::get_queue(size_t index) 
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "Queue index out of bounds.");
            return impl_->all_queues[index];
        }

        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator() 
        {
            return get_allocator(0);
        }

        /**
         * @brief Returns the closest SYCL queue for a specific NUMA node.
         * * Falls back to default queue if none found. Uses thread_local cursors
         * to achieve perfect round-robin load balancing with ZERO atomic contention.
         * * @param numa_id Target NUMA node ID.
         * @return sycl::queue& Reference to the queue.
         */
        sycl::queue& RuntimeContext::get_numa_local_queue(int numa_id)
        {
            static thread_local size_t local_numa_cursor[Core::MAX_NUMA_NODES] = {0};
            static thread_local size_t local_global_cursor = 0;

            auto it = impl_->numa_aware_queues.find(numa_id);
            if (it != impl_->numa_aware_queues.end() && !it->second.empty())
            {
                auto& queues = it->second;
                const size_t cursor = local_numa_cursor[static_cast<size_t>(numa_id)]++;
                return queues[cursor % queues.size()];
            }

            if (!impl_->all_queues.empty())
            {
                const size_t cursor = local_global_cursor++;
                SR_LOG_WARN("No queue found for NUMA {}, falling back to round-robin global queue selection.", numa_id);
                return impl_->all_queues[cursor % impl_->all_queues.size()];
            }

            SR_THROW_IF(true, "No SYCL queues are available in the runtime context.");
            return impl_->all_queues[0]; // Fallback dummy return
        }

        void* RuntimeContext::malloc_shared(size_t bytes, int numa_id)
        {
            sycl::queue& q = get_numa_local_queue(numa_id);
            void* p = sycl::malloc_shared(bytes, q);
            if (p)
            {
                // Record the owning queue by its index in all_queues so the
                // scheduler can later dispatch onto the exact same context.
                std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                for (size_t i = 0; i < impl_->all_queues.size(); ++i)
                {
                    if (impl_->all_queues[i] == q) { impl_->usm_owner[p] = i; break; }
                }
            }
            return p;
        }

        void* RuntimeContext::malloc_device(size_t bytes, int numa_id)
        {
            sycl::queue& q = get_numa_local_queue(numa_id);
            void* p = sycl::malloc_device(bytes, q);
            if (p)
            {
                std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                for (size_t i = 0; i < impl_->all_queues.size(); ++i)
                {
                    if (impl_->all_queues[i] == q) { impl_->usm_owner[p] = i; break; }
                }
            }
            return p;
        }

        void RuntimeContext::free_usm(void* ptr)
        {
            if (!ptr) return;
            size_t idx = SIZE_MAX;
            {
                std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                auto it = impl_->usm_owner.find(ptr);
                if (it != impl_->usm_owner.end())
                {
                    idx = it->second;
                    impl_->usm_owner.erase(it);
                }
            }
            // Free against the owning queue's context (required for correctness).
            if (idx < impl_->all_queues.size())
                sycl::free(ptr, impl_->all_queues[idx]);
            else if (!impl_->all_queues.empty())
                sycl::free(ptr, impl_->all_queues[0]); // best effort
        }

        sycl::queue& RuntimeContext::get_queue_for_ptr(void* ptr, int fallback_numa)
        {
            if (ptr)
            {
                // 1) Authoritative: a pointer allocated through this context's
                //    malloc_* carries an exact owning-queue record. This is the
                //    reliable path and avoids fragile pointer introspection.
                {
                    std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                    auto it = impl_->usm_owner.find(ptr);
                    if (it != impl_->usm_owner.end() && it->second < impl_->all_queues.size())
                        return impl_->all_queues[it->second];
                }
            }
            if (ptr)
            {
                // Find the queue whose CONTEXT allocated this USM. On a single
                // platform (e.g. CUDA) a malloc_shared/malloc_device pointer is
                // valid in sibling contexts too, but only the owning context
                // reports it as exactly the allocation kind it created:
                //   * device  USM -> get_pointer_type == device  in its context
                //   * shared  USM -> get_pointer_type == shared  in its context
                // A non-owning context typically reports a *different* kind (we
                // observed CUDA shared USM showing up as `device` in the CPU
                // context), so we must match device/shared against the queue's
                // device via get_pointer_device, never trust type alone.
                //
                // Strategy: prefer a queue that both (a) reports device-or-shared
                // and (b) whose own device equals get_pointer_device(). That pair
                // uniquely identifies the allocating queue. Host USM (or anything
                // we cannot pin) falls through to the NUMA-local fallback.
                // 2) Best-effort fallback for USM not allocated via this context
                //    (e.g. a test calling bare sycl::malloc_shared). A shared
                //    allocation's true owner is the context that reports it as
                //    exactly 'shared'; sibling contexts report a different kind.
                sycl::queue* best = nullptr;
                for (auto& q : impl_->all_queues)
                {
                    try
                    {
                        const sycl::context ctx = q.get_context();
                        const auto t = sycl::get_pointer_type(ptr, ctx);
                        if (t == sycl::usm::alloc::unknown || t == sycl::usm::alloc::host)
                            continue;
                        if (sycl::get_pointer_device(ptr, ctx) == q.get_device())
                        {
                            best = &q;
                            if (t == sycl::usm::alloc::shared)
                                return q;
                        }
                    }
                    catch (const sycl::exception&)
                    {
                        // Not owned by / not resolvable in this context; skip.
                    }
                }
                if (best)
                    return *best;
            }
            // Pointer-less (e.g. host task) or host-allocated data: keep the
            // existing NUMA-local round-robin behaviour.
            return get_numa_local_queue(fallback_numa);
        }

        /**
         * @brief Returns a NUMA-aware USM allocator (lazy initialization).
         * 
         * Creates a new allocator atomically if it doesn't exist.
         * 
         * @param numa_id Target NUMA node ID.
         * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> The allocator.
         */
        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator(int numa_id)
        {
            if (numa_id < 0 || static_cast<size_t>(numa_id) >= Core::MAX_NUMA_NODES) 
            {
                SR_LOG_WARN("Invalid NUMA ID {}, falling back to default.", numa_id);
                return get_allocator();
            }

            /** @brief Step 1: fast path - check if exists. */
            auto* existing = impl_->numa_allocators[numa_id].load(std::memory_order_acquire);
            if (existing) 
            {
                return sushi_ptr<SushiRuntime::Memory::USMAllocator>(existing);
            }

            /** @brief Step 2: slow path - create and try atomic swap. */
            SR_LOG_DEBUG("Initializing allocator for NUMA {} (potential race)", numa_id);
            sycl::queue& q = get_numa_local_queue(numa_id);
            auto new_alloc_sushi = SushiRuntime::Memory::create_allocator(q, numa_id);
            auto* new_alloc_raw = new_alloc_sushi.get();

            new_alloc_raw->add_ref();

            SushiRuntime::Memory::USMAllocator* expected = nullptr;
            
            /** @brief Step 3: compare-and-swap (CAS). */
            if (impl_->numa_allocators[numa_id].compare_exchange_strong(expected, new_alloc_raw, 
                                                                        std::memory_order_release, 
                                                                        std::memory_order_acquire)) 
            {
                return new_alloc_sushi;
            }
            else 
            {
                new_alloc_raw->release();
                return sushi_ptr<SushiRuntime::Memory::USMAllocator>(expected);
            }
        }

        int RuntimeContext::numa_distance(int numa_a, int numa_b) const
        {
            return impl_->distance_model->distance(numa_a, numa_b);
        }

        const std::vector<int>& RuntimeContext::cpu_core_to_numa() const
        {
            return impl_->core_to_numa;
        }

        void RuntimeContext::wait_all()
        {
            for (auto& q : impl_->all_queues) 
            {
                q.wait_and_throw();
            }
        }

        IExecutionEngine& RuntimeContext::get_execution_engine()
        {
            SR_THROW_IF(!impl_->engine, "ExecutionEngine is not initialized. Was probe_hardware() called?");
            return *impl_->engine;
        }

        void RuntimeContext::shutdown()
        {
            if (impl_->engine)
            {
                impl_->engine->shutdown();
            }
        }

#ifdef SUSHI_DISTRIBUTED
        void RuntimeContext::enable_distributed_master(
            std::shared_ptr<Distributed::ITransport> transport,
            sushi_ptr<Distributed::KernelRegistry>   kernels,
            std::shared_ptr<Distributed::IOffloadPolicy> policy)
        {
            SR_THROW_IF(!transport, "enable_distributed_master requires a transport.");
            SR_THROW_IF(!impl_->engine, "ExecutionEngine must be initialized before Master mode.");

            if (!policy)
                policy = std::make_shared<Distributed::LeastLoadedOffloadPolicy>();

            impl_->buffers = std::make_unique<Distributed::BufferRegistry>();
            impl_->coordinator = std::make_unique<Distributed::DistributedCoordinator>(
                std::move(kernels), *impl_->buffers, std::move(transport), std::move(policy));

            // Wire the completion hook to the persistent scheduler, then attach
            // the coordinator so the dispatch loop's offload branch goes live.
            auto& sched = impl_->engine->scheduler_ref();
            impl_->coordinator->set_completion_hook(
                [&sched](Graph::Node* node, bool success)
                {
                    sched.complete_offloaded_node(node, success);
                });

            // Install the coordinator (and, with it, the resubmit hook) BEFORE the
            // monitor thread starts. Otherwise a worker reaped in the first liveness
            // window could fire on_resubmit while it is still null.
            sched.set_coordinator(impl_->coordinator.get());
            impl_->coordinator->start();
            impl_->role = RuntimeRole::Master;

            SR_LOG_INFO("RuntimeContext promoted to distributed Master mode.");
        }

        Distributed::BufferHandle RuntimeContext::register_distributed_buffer(void* usm_ptr, uint64_t size_bytes)
        {
            SR_THROW_IF(!impl_->buffers, "register_distributed_buffer requires Master mode.");
            return impl_->buffers->register_buffer(usm_ptr, size_bytes);
        }

        void RuntimeContext::bump_distributed_buffer(void* usm_ptr)
        {
            if (impl_->buffers)
                impl_->buffers->bump_version(usm_ptr);
        }

        std::optional<Distributed::BufferHandle> RuntimeContext::distributed_buffer(void* usm_ptr) const
        {
            if (!impl_->buffers)
                return std::nullopt;
            return impl_->buffers->lookup(usm_ptr);
        }

        bool RuntimeContext::distributed_supports(Graph::OpID op) const
        {
            return impl_->coordinator && impl_->coordinator->supports(op);
        }

        RuntimeContext::RuntimeRole RuntimeContext::distributed_role() const
        {
            return impl_->role;
        }

        uint64_t RuntimeContext::distributed_offloaded_count() const
        {
            return impl_->coordinator ? impl_->coordinator->offloaded_count() : 0;
        }

        uint64_t RuntimeContext::distributed_declined_count() const
        {
            return impl_->coordinator ? impl_->coordinator->declined_count() : 0;
        }

        uint64_t RuntimeContext::distributed_redispatched_count() const
        {
            return impl_->coordinator ? impl_->coordinator->redispatched_count() : 0;
        }

        void RuntimeContext::distributed_set_liveness_timeout(std::chrono::milliseconds timeout)
        {
            if (impl_->coordinator)
                impl_->coordinator->set_liveness_timeout(timeout);
        }

        int RuntimeContext::distributed_alive_workers() const
        {
            return impl_->coordinator ? impl_->coordinator->alive_worker_count() : 0;
        }
#endif

    } // namespace Execution
} // namespace SushiRuntime