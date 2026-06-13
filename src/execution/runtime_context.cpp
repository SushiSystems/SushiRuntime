/**************************************************************************/
/* runtime_context.cpp                                                    */
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

#include <map>
#include <array>
#include <atomic>
#include <memory>
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

            impl_->coordinator->start();
            sched.set_coordinator(impl_->coordinator.get());
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

        int RuntimeContext::distributed_alive_workers() const
        {
            return impl_->coordinator ? impl_->coordinator->alive_worker_count() : 0;
        }
#endif

    } // namespace Execution
} // namespace SushiRuntime