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
#ifdef SR_DISTRIBUTED
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
             * @brief Whether device queues were built with profiling enabled.
             *
             * Set once at construction. Queues carry the SYCL profiling property
             * only when this is true, so the per-node kernel timing in RunReport is
             * available; left false (the default) the queues are built exactly as
             * before and the hot path reads no timestamps.
             */
            bool profiling = false;

            /**
             * @brief Cumulative bytes the runtime itself copied across the PCIe link.
             *
             * Counts only the explicit, runtime-initiated transfers — the selective
             * readback in Buffer::read_range / write_range (and the distributed path)
             * — so RunReport can report how much the host pulled from and pushed to
             * the device during a run. Implicit shared-USM page migration by the
             * driver is invisible to SYCL and deliberately not counted here. Relaxed
             * atomics: the values are monotonic counters read at a run boundary, with
             * no ordering dependency on other state.
             */
            std::atomic<std::size_t> device_to_host_bytes{0};
            std::atomic<std::size_t> host_to_device_bytes{0};

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

#ifdef SR_DISTRIBUTED
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

#ifdef SR_DISTRIBUTED
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
                    // Swallow a late async device error here: this runs in the
                    // Runtime destructor (implicitly noexcept), so a throwing
                    // wait_and_throw() would call std::terminate. We are tearing
                    // down and cannot surface it to a caller — log and continue so
                    // the remaining queues still drain and resources still free.
                    try
                    {
                        q.wait_and_throw();
                    }
                    catch (const std::exception& ex)
                    {
                        SR_LOG_WARN("Async device error during shutdown drain: {}", ex.what());
                    }
                    catch (...)
                    {
                        SR_LOG_WARN("Unknown async device error during shutdown drain.");
                    }
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

        /**
         * @brief Constructs a RuntimeContext with optional hardware configuration and topology providers.
         *
         * Initializes the compute stack, probes available hardware, and prepares the execution engine.
         *
         * @param config The hardware filtering configuration.
         * @param provider Optional custom topology provider.
         * @param distance_model Optional custom NUMA distance model.
         */
        RuntimeContext::RuntimeContext(const Topology::HardwareFilterConfig& config,
                                       std::shared_ptr<Topology::ITopologyProvider> provider,
                                       std::shared_ptr<Topology::INumaDistanceModel> distance_model,
                                       bool profiling)
            : impl_(make_sushi<Impl>())
        {
            SR_LOG_INFO("Initializing compute stack...");

            // Record the profiling choice before probe_hardware() builds the queues,
            // so the property is applied while they are created (queues are built
            // once and not rebuilt to toggle profiling).
            impl_->profiling = profiling;

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

        /**
         * @brief Destroys the RuntimeContext.
         *
         * Cleans up all managed hardware, allocators, and execution resources.
         */
        RuntimeContext::~RuntimeContext() = default;

        /**
         * @brief Reports whether the queues were built with profiling enabled.
         */
        bool RuntimeContext::profiling_enabled() const noexcept
        {
            return impl_->profiling;
        }

        /**
         * @brief Adds to the device->host explicit-transfer byte counter.
         */
        void RuntimeContext::add_device_to_host_bytes(std::size_t bytes) noexcept
        {
            impl_->device_to_host_bytes.fetch_add(bytes, std::memory_order_relaxed);
        }

        /**
         * @brief Adds to the host->device explicit-transfer byte counter.
         */
        void RuntimeContext::add_host_to_device_bytes(std::size_t bytes) noexcept
        {
            impl_->host_to_device_bytes.fetch_add(bytes, std::memory_order_relaxed);
        }

        /**
         * @brief Returns cumulative device->host explicit-transfer bytes.
         */
        std::size_t RuntimeContext::device_to_host_bytes() const noexcept
        {
            return impl_->device_to_host_bytes.load(std::memory_order_relaxed);
        }

        /**
         * @brief Returns cumulative host->device explicit-transfer bytes.
         */
        std::size_t RuntimeContext::host_to_device_bytes() const noexcept
        {
            return impl_->host_to_device_bytes.load(std::memory_order_relaxed);
        }

        /**
         * @brief Executes the hardware discovery and topology mapping process.
         *
         * Initializes the device inventory and configures SYCL queues based on NUMA nodes.
         *
         * @param config The configuration containing hardware filtering preferences.
         * @throws std::exception If a critical initialization failure occurs.
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
                    // FPGAs or custom accelerators often execute better in-order or
                    // don't support full OOO queues natively. Profiling is added on
                    // top when the context was built for it, so RunReport can read
                    // each kernel's device start/end timestamps; the two properties
                    // compose, so the FPGA case keeps its in-order guarantee.
                    const bool needs_in_order =
                        !node.sycl_dev.is_cpu() && !node.sycl_dev.is_gpu();
                    sycl::property_list props;
                    if (needs_in_order && impl_->profiling)
                        props = { sycl::property::queue::in_order(),
                                  sycl::property::queue::enable_profiling() };
                    else if (needs_in_order)
                        props = { sycl::property::queue::in_order() };
                    else if (impl_->profiling)
                        props = { sycl::property::queue::enable_profiling() };
                    
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

        /**
         * @brief Retrieves a SYCL queue by its global index.
         *
         * @param index The global index of the queue.
         * @return sycl::queue& Reference to the requested queue.
         * @throws std::runtime_error If the queue index is out of bounds.
         */
        sycl::queue& RuntimeContext::get_queue(size_t index) 
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "Queue index out of bounds.");
            return impl_->all_queues[index];
        }

        /**
         * @brief Gets the total number of available devices (queues).
         *
         * @return size_t The count of devices.
         */
        size_t RuntimeContext::device_count() const
        {
            return impl_->all_queues.size();
        }

        /**
         * @brief Retrieves the default USM allocator for NUMA node 0.
         *
         * @return sushi_ptr<SushiRuntime::Memory::USMAllocator> A pointer to the USM allocator.
         */
        sushi_ptr<SushiRuntime::Memory::USMAllocator> RuntimeContext::get_allocator() 
        {
            return get_allocator(0);
        }

        /**
         * @brief Returns the closest SYCL queue for a specific NUMA node.
         *
         * Falls back to a default queue if none is found. Uses thread_local cursors
         * to achieve perfect round-robin load balancing with zero atomic contention.
         *
         * @param numa_id Target NUMA node ID.
         * @return sycl::queue& Reference to the queue.
         * @throws std::runtime_error If no SYCL queues are available.
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
         * @brief Allocates shared USM memory on a specific device.
         *
         * @param bytes The size in bytes to allocate.
         * @param device_index The index of the device to allocate on.
         * @return void* Pointer to the allocated memory.
         * @throws std::runtime_error If the device index is out of bounds.
         */
        void* RuntimeContext::malloc_shared_on(size_t bytes, size_t device_index)
        {
            sycl::queue& q = get_queue(device_index); // throws if out of bounds
            void* p = sycl::malloc_shared(bytes, q);
            if (p)
            {
                std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                impl_->usm_owner[p] = device_index;
            }
            return p;
        }

        /**
         * @brief Allocates device-only USM on a specific device queue.
         *
         * @param bytes The size in bytes to allocate.
         * @param device_index The index of the device to allocate on.
         * @return void* Pointer to the allocated device memory.
         * @throws std::runtime_error If the device index is out of bounds.
         */
        void* RuntimeContext::malloc_device_on(size_t bytes, size_t device_index)
        {
            sycl::queue& q = get_queue(device_index); // throws if out of bounds
            void* p = sycl::malloc_device(bytes, q);
            if (p)
            {
                std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
                impl_->usm_owner[p] = device_index;
            }
            return p;
        }

        /**
         * @brief Allocates shared USM memory on a specific NUMA node.
         *
         * @param bytes The size in bytes to allocate.
         * @param numa_id The NUMA node ID specifying the locality.
         * @return void* Pointer to the allocated memory.
         */
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

        /**
         * @brief Allocates device-only USM memory on a specific NUMA node.
         *
         * @param bytes The size in bytes to allocate.
         * @param numa_id The NUMA node ID specifying the locality.
         * @return void* Pointer to the allocated memory.
         */
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

        /**
         * @brief Returns the owning device-queue index for a USM pointer, or -1.
         */
        int RuntimeContext::context_index_for_ptr(void* ptr) noexcept
        {
            if (!ptr) return -1;
            std::lock_guard<std::mutex> lk(impl_->usm_owner_mtx);
            auto it = impl_->usm_owner.find(ptr);
            if (it != impl_->usm_owner.end() && it->second < impl_->all_queues.size())
                return static_cast<int>(it->second);
            return -1;
        }

        /**
         * @brief Frees memory that was allocated via USM.
         *
         * Releases the memory using the appropriate context queue that owns it.
         *
         * @param ptr Pointer to the allocated memory.
         */
        void RuntimeContext::free_usm(void* ptr)
        {
            if (!ptr) return;

#ifdef SR_DISTRIBUTED
            unregister_distributed_buffer(ptr);
#endif

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

        /**
         * @brief Retrieves the queue associated with the context that allocated the given USM pointer.
         *
         * First relies on an authoritative internal tracking map. As a fallback, uses SYCL
         * pointer introspection.
         *
         * @param ptr The USM pointer to inspect.
         * @param fallback_numa The NUMA node ID to fallback to if the owner cannot be determined.
         * @return sycl::queue& Reference to the associated queue.
         */
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

        /**
         * @brief Computes the distance between two NUMA nodes.
         *
         * Uses the internal distance model to determine proximity.
         *
         * @param numa_a The first NUMA node ID.
         * @param numa_b The second NUMA node ID.
         * @return int The distance metric between the two nodes.
         */
        int RuntimeContext::numa_distance(int numa_a, int numa_b) const
        {
            return impl_->distance_model->distance(numa_a, numa_b);
        }

        /**
         * @brief Retrieves the mapping from logical CPU cores to NUMA nodes.
         *
         * @return const std::vector<int>& A vector mapping core index to NUMA node ID.
         */
        const std::vector<int>& RuntimeContext::cpu_core_to_numa() const
        {
            return impl_->core_to_numa;
        }

        /**
         * @brief Waits for all queues in the runtime context to finish execution.
         *
         * This function acts as a global synchronization point.
         *
         * @throws sycl::exception If an error occurs during queue execution.
         */
        void RuntimeContext::wait_all()
        {
            for (auto& q : impl_->all_queues) 
            {
                q.wait_and_throw();
            }
        }

        /**
         * @brief Gets a reference to the active execution engine.
         *
         * @return IExecutionEngine& A reference to the execution engine instance.
         * @throws std::runtime_error If the execution engine is not initialized.
         */
        IExecutionEngine& RuntimeContext::get_execution_engine()
        {
            SR_THROW_IF(!impl_->engine, "ExecutionEngine is not initialized. Was probe_hardware() called?");
            return *impl_->engine;
        }

        /**
         * @brief Shuts down the runtime context and its execution engine.
         *
         * Safely stops internal components before full destruction.
         */
        void RuntimeContext::shutdown()
        {
            if (impl_->engine)
            {
                impl_->engine->shutdown();
            }
        }

#ifdef SR_DISTRIBUTED
        /**
         * @brief Enables distributed Master mode on the runtime context.
         *
         * Initializes network communication and coordinator for remote task offloading.
         *
         * @param transport The transport layer for communication.
         * @param kernels The kernel registry to be used across nodes.
         * @param policy The strategy used to offload tasks to workers.
         * @throws std::runtime_error If the transport is null or execution engine is unavailable.
         */
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

        /**
         * @brief Registers a USM buffer for distributed tracking.
         *
         * @param usm_ptr The raw pointer to the buffer.
         * @param size_bytes The size of the buffer in bytes.
         * @return Distributed::BufferHandle A handle representing the registered buffer.
         * @throws std::runtime_error If the context is not in Master mode.
         */
        Distributed::BufferHandle RuntimeContext::register_distributed_buffer(void* usm_ptr, uint64_t size_bytes)
        {
            SR_THROW_IF(!impl_->buffers, "register_distributed_buffer requires Master mode.");
            return impl_->buffers->register_buffer(usm_ptr, size_bytes);
        }

        /**
         * @brief Bumps the version of a registered distributed buffer.
         *
         * @param usm_ptr The raw pointer to the buffer.
         */
        void RuntimeContext::bump_distributed_buffer(void* usm_ptr)
        {
            if (impl_->buffers)
                impl_->buffers->bump_version(usm_ptr);
        }

        /**
         * @brief Unregisters a distributed buffer from tracking.
         *
         * @param usm_ptr The raw pointer to the buffer.
         */
        void RuntimeContext::unregister_distributed_buffer(void* usm_ptr)
        {
            if (impl_->buffers)
                impl_->buffers->unregister_buffer(usm_ptr);
        }

        /**
         * @brief Looks up the distributed buffer handle for a given pointer.
         *
         * @param usm_ptr The raw pointer to query.
         * @return std::optional<Distributed::BufferHandle> The buffer handle if found, otherwise std::nullopt.
         */
        std::optional<Distributed::BufferHandle> RuntimeContext::distributed_buffer(void* usm_ptr) const
        {
            if (!impl_->buffers)
                return std::nullopt;
            return impl_->buffers->lookup(usm_ptr);
        }

        /**
         * @brief Checks if a specific distributed operation is supported.
         *
         * @param op The operation identifier.
         * @return true If the operation is supported.
         * @return false Otherwise.
         */
        bool RuntimeContext::distributed_supports(Graph::OpID op) const
        {
            return impl_->coordinator && impl_->coordinator->supports(op);
        }

        /**
         * @brief Gets the current distributed role of the context.
         *
         * @return RuntimeContext::RuntimeRole The role (e.g., Standalone, Master).
         */
        RuntimeContext::RuntimeRole RuntimeContext::distributed_role() const
        {
            return impl_->role;
        }

        /**
         * @brief Gets the total count of distributed tasks offloaded.
         *
         * @return uint64_t The number of offloaded tasks.
         */
        uint64_t RuntimeContext::distributed_offloaded_count() const
        {
            return impl_->coordinator ? impl_->coordinator->offloaded_count() : 0;
        }

        /**
         * @brief Gets the total count of distributed tasks declined by workers.
         *
         * @return uint64_t The number of declined tasks.
         */
        uint64_t RuntimeContext::distributed_declined_count() const
        {
            return impl_->coordinator ? impl_->coordinator->declined_count() : 0;
        }

        /**
         * @brief Gets the total count of distributed tasks that were redispatched.
         *
         * @return uint64_t The number of redispatched tasks.
         */
        uint64_t RuntimeContext::distributed_redispatched_count() const
        {
            return impl_->coordinator ? impl_->coordinator->redispatched_count() : 0;
        }

        /**
         * @brief Gets the total count of stale writebacks in distributed executions.
         *
         * @return uint64_t The number of stale writeback operations.
         */
        uint64_t RuntimeContext::distributed_stale_writeback_count() const
        {
            return impl_->coordinator ? impl_->coordinator->stale_writeback_count() : 0;
        }

        /**
         * @brief Sets the liveness timeout for distributed worker nodes.
         *
         * @param timeout The duration before a worker is considered unresponsive.
         */
        void RuntimeContext::distributed_set_liveness_timeout(std::chrono::milliseconds timeout)
        {
            if (impl_->coordinator)
                impl_->coordinator->set_liveness_timeout(timeout);
        }

        /**
         * @brief Sets the deadline for a distributed task to complete.
         *
         * @param deadline The maximum duration allowed for a task.
         */
        void RuntimeContext::distributed_set_task_deadline(std::chrono::milliseconds deadline)
        {
            if (impl_->coordinator)
                impl_->coordinator->set_task_deadline(deadline);
        }

        /**
         * @brief Gets the current count of alive workers in the cluster.
         *
         * @return int The number of active remote workers.
         */
        int RuntimeContext::distributed_alive_workers() const
        {
            return impl_->coordinator ? impl_->coordinator->alive_worker_count() : 0;
        }
#endif

    } // namespace Execution
} // namespace SushiRuntime