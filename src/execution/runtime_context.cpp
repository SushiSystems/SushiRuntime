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
#include <optional>
#include <unordered_map>
#include <SushiRuntime/core/error.hpp>
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
             * @brief Per-device profiling-enabled queue, built lazily on request.
             *
             * Sized to nodes.size() once in probe_hardware() and never resized
             * again, so a reference handed out by profiling_queue() stays valid
             * for the context's lifetime. Empty until the device's entry in
             * profiling_once fires, either eagerly (settings.profiling) or on
             * the first profiling_queue() call for that device.
             */
            std::vector<std::optional<sycl::queue>> profiling_queues;

            /** @brief One-shot guard per device for building its profiling_queues entry. */
            std::vector<std::once_flag> profiling_once;

            /**
             * @brief Publishes that profiling_queues[i] is fully constructed.
             * has_profiling_queue() reads this instead of profiling_queues[i]'s
             * optional, whose emplace() a concurrent call_once may still be running.
             */
            std::vector<std::atomic<bool>> profiling_built;

            /** @brief Set once a device's profiling chain queue exists; read like profiling_built. */
            std::vector<std::atomic<bool>> chain_profiling_built;

            /** @brief Holds one lazily built queue and the guard that builds it once. */
            struct LazyQueue
            {
                std::optional<sycl::queue> queue; /**< Empty until once fires. */
                std::once_flag once;              /**< Guards the single build of queue. */
            };

            /**
             * @brief Per-device in-order queues a chainable plan submits to, plain then profiling.
             *
             * Sized to nodes.size() once in probe_hardware() and never resized, so
             * a reference chain_queue() hands out stays valid for the context's
             * lifetime.
             */
            std::vector<LazyQueue> chain_queues;
            std::vector<LazyQueue> chain_profiling_queues;

            /**
             * @brief The runtime-wide settings this context was created with.
             *
             * Set once at construction and never mutated: the scheduler sizes and
             * places its worker pool from it, the execution engine reads the
             * rebalancer choice, and probe_hardware() reads the profiling choice
             * while the queues are being built (a SYCL queue carries the profiling
             * property from creation and is not rebuilt to toggle it). Immutability
             * is what makes it safe to read from worker threads without a lock.
             */
            RuntimeSettings settings;

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
            SushiRuntime::Memory::UsmRegistry usm_registry;

            /**
             * @brief Execution engine (owns persistent TaskScheduler).
             *
             * Stored as a raw pointer and initialised lazily after probe_hardware()
             * so that the engine can receive a fully-initialised queue list.
             * Lifetime is always shorter than the Impl itself — shutdown() is called
             * in ~Impl before any other resource is released.
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

            ~Impl() 
            {
                // Destruction order is load-bearing. The ExecutionEngine goes first,
                // which joins every worker thread and so removes any dangling
                // reference to a queue or another Impl member. Only then is it safe
                // to drain outstanding GPU work with wait_and_throw(), so that every
                // hardware operation has completed before the SYCL objects it used
                // go out of scope.

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

                auto drain = [](sycl::queue& q)
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
                };

                for (auto& q : all_queues) drain(q);
                for (auto& q : profiling_queues) if (q) drain(*q);
                for (auto& q : chain_queues) if (q.queue) drain(*q.queue);
                for (auto& q : chain_profiling_queues) if (q.queue) drain(*q.queue);
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
                                       const RuntimeSettings& settings)
            : impl_(make_sushi<Impl>())
        {
            SR_LOG_INFO("Initializing compute stack...");

            // Recorded before probe_hardware() builds the queues, so the profiling
            // property is applied while they are created (queues are built once and
            // not rebuilt to toggle it), and before the ExecutionEngine below reads
            // the worker-pool and rebalancer choices out of it.
            impl_->settings = settings;

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
            // The scheduler it starts is persistent: it lives for the whole
            // lifetime of the RuntimeContext rather than being rebuilt per run.
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
            return impl_->settings.profiling;
        }

        /**
         * @brief Returns the runtime-wide settings fixed at construction.
         */
        const RuntimeSettings& RuntimeContext::settings() const noexcept
        {
            return impl_->settings;
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

                // Move-assigned, not resized: once_flag and atomic elements are not movable.
                impl_->profiling_queues = std::vector<std::optional<sycl::queue>>(impl_->nodes.size());
                impl_->profiling_once = std::vector<std::once_flag>(impl_->nodes.size());
                impl_->profiling_built = std::vector<std::atomic<bool>>(impl_->nodes.size());
                impl_->chain_profiling_built = std::vector<std::atomic<bool>>(impl_->nodes.size());
                impl_->chain_queues = std::vector<Impl::LazyQueue>(impl_->nodes.size());
                impl_->chain_profiling_queues = std::vector<Impl::LazyQueue>(impl_->nodes.size());

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
                    if (needs_in_order && impl_->settings.profiling)
                        props = { sycl::property::queue::in_order(),
                                  sycl::property::queue::enable_profiling() };
                    else if (needs_in_order)
                        props = { sycl::property::queue::in_order() };
                    else if (impl_->settings.profiling)
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

                // Reported here, where the filter that caused it is still in hand.
                // Anything later asks for queue 0 and fails with
                // "Queue index out of bounds" — true, but it names neither the
                // machine's state nor the filter that produced it, and it is the
                // first thing a player on a GPU-less machine would see. BB-5b's
                // try_create() turns this into a returned Error rather than a throw.
                if (impl_->all_queues.empty())
                {
                    std::string detail = "No SYCL device satisfied the hardware filter. Active filter: ";
                    detail += "cpu=";           detail += config.include_cpu ? "yes" : "no";
                    detail += ", gpu=";         detail += config.include_gpu ? "yes" : "no";
                    detail += ", accelerator="; detail += config.include_accelerator ? "yes" : "no";
                    detail += ", require_fp64="; detail += config.require_fp64 ? "yes" : "no";
                    detail += ". Either the machine exposes no SYCL device (no GPU driver and no "
                              "OpenCL CPU runtime installed) or every device it exposes was excluded.";

                    // The enclosing catch logs it; throwing unlogged avoids a duplicate line.
                    throw Core::SushiException(Core::Error{Core::Errc::unsupported, std::move(detail)});
                }

                SR_LOG_INFO("Infrastructure ready. Total devices: {}, NUMA domains: {}",
                            impl_->all_queues.size(),
                            impl_->numa_aware_queues.size());

                // Eager build: a profiled run must not pay first-touch latency.
                if (impl_->settings.profiling)
                {
                    for (std::size_t i = 0; i < impl_->all_queues.size(); ++i)
                        build_profiling_queue(i);
                }
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
         * @brief Builds device @p index's profiling queue exactly once.
         *
         * Shares the plain queue's context and device, adds enable_profiling,
         * and keeps in_order when the plain queue carries it (the FPGA case in
         * probe_hardware()) so the two properties still compose. When
         * settings.profiling is true the plain queue already carries
         * enable_profiling (probe_hardware()'s existing behaviour, kept as-is
         * for existing consumers), so this stores the plain queue itself
         * instead of building a duplicate second queue.
         */
        void RuntimeContext::build_profiling_queue(size_t index)
        {
            std::call_once(impl_->profiling_once[index], [this, index]()
            {
                sycl::queue& plain = impl_->all_queues[index];
                if (plain.has_property<sycl::property::queue::enable_profiling>())
                {
                    impl_->profiling_queues[index].emplace(plain);
                }
                else
                {
                    const sycl::property_list props = plain.has_property<sycl::property::queue::in_order>()
                        ? sycl::property_list{ sycl::property::queue::in_order(),
                                                sycl::property::queue::enable_profiling() }
                        : sycl::property_list{ sycl::property::queue::enable_profiling() };

                    impl_->profiling_queues[index].emplace(plain.get_context(), plain.get_device(),
                        [](sycl::exception_list elist)
                        {
                            for (auto e : elist) std::rethrow_exception(e);
                        },
                        props);
                }

                // Stored last: has_profiling_queue() reads only this flag, never the optional.
                impl_->profiling_built[index].store(true, std::memory_order_release);
            });
        }

        /**
         * @brief Returns device @p index's profiling-enabled queue, building it on first use.
         */
        sycl::queue& RuntimeContext::profiling_queue(size_t index)
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "Queue index out of bounds.");
            build_profiling_queue(index);
            return *impl_->profiling_queues[index];
        }

        /**
         * @brief Reports whether device @p index's profiling queue has been built.
         */
        bool RuntimeContext::has_profiling_queue(size_t index) const noexcept
        {
            return index < impl_->profiling_built.size()
                && (impl_->profiling_built[index].load(std::memory_order_acquire)
                    || impl_->chain_profiling_built[index].load(std::memory_order_acquire));
        }

        /**
         * @brief Builds device @p index's in-order chain queue of one flavour exactly once.
         *
         * Shares the plain queue's context and device and always carries in_order;
         * the profiling flavour adds enable_profiling on top.
         */
        void RuntimeContext::build_chain_queue(size_t index, bool profiling)
        {
            auto& slot = profiling ? impl_->chain_profiling_queues[index] : impl_->chain_queues[index];
            std::call_once(slot.once, [this, index, profiling, &slot]()
            {
                sycl::queue& plain = impl_->all_queues[index];
                const sycl::property_list props = profiling
                    ? sycl::property_list{ sycl::property::queue::in_order(),
                                           sycl::property::queue::enable_profiling() }
                    : sycl::property_list{ sycl::property::queue::in_order() };

                slot.queue.emplace(plain.get_context(), plain.get_device(),
                    [](sycl::exception_list elist)
                    {
                        for (auto e : elist) std::rethrow_exception(e);
                    },
                    props);
                if (profiling)
                    impl_->chain_profiling_built[index].store(true, std::memory_order_release);
            });
        }

        /**
         * @brief Returns device @p index's in-order chain queue, building it on first use.
         */
        sycl::queue& RuntimeContext::chain_queue(size_t index, bool profiling)
        {
            SR_THROW_IF(index >= impl_->all_queues.size(), "Queue index out of bounds.");
            build_chain_queue(index, profiling);
            return profiling ? *impl_->chain_profiling_queues[index].queue
                             : *impl_->chain_queues[index].queue;
        }

        /**
         * @brief Reports whether the device behind a queue is a GPU.
         *
         * Reads the flag probe_hardware() recorded, which is what makes it cheap and
         * allocation-free on every platform. all_queues is filled by walking
         * impl_->nodes in order, one queue per node with no skips, so the two share
         * an index.
         *
         * @param index The global index of the queue.
         * @return True when that device is a GPU.
         * @throws Core::SushiException If the queue index is out of bounds.
         */
        bool RuntimeContext::device_is_gpu(size_t index) const
        {
            SR_THROW_IF(index >= impl_->nodes.size(), "Queue index out of bounds.");
            return impl_->nodes[index].is_gpu;
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
                impl_->usm_registry.record(p, device_index, bytes);
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
                impl_->usm_registry.record(p, device_index, bytes);
            }
            return p;
        }

        /**
         * @brief Allocates pinned host USM in a specific device queue's context.
         *
         * @param bytes The size in bytes to allocate.
         * @param device_index The index of the device whose context owns the memory.
         * @return void* Pointer to the allocated host memory.
         * @throws std::runtime_error If the device index is out of bounds.
         */
        void* RuntimeContext::malloc_host_on(size_t bytes, size_t device_index)
        {
            sycl::queue& q = get_queue(device_index); // throws if out of bounds
            void* p = sycl::malloc_host(bytes, q);
            if (p)
            {
                impl_->usm_registry.record(p, device_index, bytes);
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
                for (size_t i = 0; i < impl_->all_queues.size(); ++i)
                {
                    if (impl_->all_queues[i] == q) { impl_->usm_registry.record(p, i, bytes); break; }
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
                for (size_t i = 0; i < impl_->all_queues.size(); ++i)
                {
                    if (impl_->all_queues[i] == q) { impl_->usm_registry.record(p, i, bytes); break; }
                }
            }
            return p;
        }

        /**
         * @brief Allocates host USM aligned to @p alignment on a specific device queue.
         *
         * @param alignment    Byte alignment of the returned address.
         * @param bytes        The size in bytes to allocate.
         * @param device_index The index of the device to allocate on.
         * @return void* Host-addressable pointer, or nullptr on failure.
         */
        void* RuntimeContext::aligned_malloc_host_on(size_t alignment, size_t bytes, size_t device_index)
        {
            sycl::queue& q = get_queue(device_index); // throws if out of bounds
            void* p = sycl::aligned_alloc_host(alignment, bytes, q);
            if (p)
                impl_->usm_registry.record(p, device_index, bytes);
            return p;
        }

        /**
         * @brief Records an externally-allocated address as owned by a device queue.
         *
         * @param address      The address to record; not allocated by this call.
         * @param device_index The device queue that owns @p address.
         * @param bytes        Length of the mapping, or nothing when it is unknown.
         */
        void RuntimeContext::adopt_usm(void* address, size_t device_index,
                                       std::optional<std::size_t> bytes)
        {
            get_queue(device_index); // throws if out of bounds
            impl_->usm_registry.record(address, device_index, bytes);
        }

        /**
         * @brief Finds the start of the recorded allocation an address falls in.
         *
         * Resolves an interior pointer to the base that owner_of() knows, for
         * callers validating a pointer a user handed them. Linear in live
         * allocations; a registration-time query, not a dispatch-time one.
         *
         * @param ptr Any address, interior or base. Null resolves to nothing.
         * @return The allocation's first address, or nothing when none covers @p ptr.
         */
        std::optional<void*> RuntimeContext::containing_usm_base(void* ptr) const
        {
            const auto found = impl_->usm_registry.containing_allocation(ptr);
            return found ? std::optional<void*>(found->base) : std::nullopt;
        }

        /**
         * @brief Drops an address's owner record without freeing it.
         * @param address The address to forget; safe to call with nullptr.
         */
        void RuntimeContext::forget_usm(void* address) noexcept
        {
            impl_->usm_registry.take(address);
        }

        /**
         * @brief Returns the owning device-queue index for a USM pointer, or -1.
         */
        int RuntimeContext::context_index_for_ptr(void* ptr) noexcept
        {
            if (!ptr) return -1;
            const auto owner = impl_->usm_registry.owner_of(ptr);
            if (owner && *owner < impl_->all_queues.size())
                return static_cast<int>(*owner);
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

            // Resolve and forget in one step: two separate locks would let a second
            // free of the same pointer read an owner this one is already acting on.
            const size_t idx = impl_->usm_registry.take(ptr).value_or(SIZE_MAX);
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
                const auto owner = impl_->usm_registry.owner_of(ptr);
                if (owner && *owner < impl_->all_queues.size())
                    return impl_->all_queues[*owner];
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