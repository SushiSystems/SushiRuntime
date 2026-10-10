/**************************************************************************/
/* runtime_context.cpp                                                    */
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
 * @file runtime_context.cpp
 * @brief Probes the hardware, builds the context's parts in order and tears them down.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#include <SushiRuntime/execution/runtime_context.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/device/device_info.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>

namespace SushiRuntime
{
    namespace Execution
    {
        namespace
        {
            /** @brief Converts a discovered hardware node to the descriptor a device set takes. */
            Device::DeviceDescriptor describe_device(const Topology::HardwareNode& node)
            {
                Device::DeviceDescriptor descriptor;
                descriptor.device = node.sycl_dev;
                descriptor.info.name = node.name;
                descriptor.info.backend = node.sycl_dev.get_backend();
                descriptor.info.numa_node = node.numa_node_id;
                descriptor.info.memory_bytes = node.global_mem_size;
                descriptor.info.compute_units = node.max_compute_units;

                // The node's own flag names a GPU, so device_is_gpu() answers as discovery did.
                if (node.is_gpu)
                    descriptor.info.kind = Device::DeviceKind::Gpu;
                else if (node.sycl_dev.is_cpu())
                    descriptor.info.kind = Device::DeviceKind::Cpu;
                else if (node.sycl_dev.is_accelerator())
                    descriptor.info.kind = Device::DeviceKind::Accelerator;
                else
                    descriptor.info.kind = Device::DeviceKind::Other;

                return descriptor;
            }

            /** @brief Builds the exception for a filter that no SYCL device passed. */
            Core::SushiException no_device_error(const Topology::HardwareFilterConfig& config)
            {
                std::string detail = "No SYCL device satisfied the hardware filter. Active filter: ";
                detail += "cpu=";
                detail += config.include_cpu ? "yes" : "no";
                detail += ", gpu=";
                detail += config.include_gpu ? "yes" : "no";
                detail += ", accelerator=";
                detail += config.include_accelerator ? "yes" : "no";
                detail += ", require_fp64=";
                detail += config.require_fp64 ? "yes" : "no";
                detail += ". Either the machine exposes no SYCL device (no GPU driver and no "
                          "OpenCL CPU runtime installed) or every device it exposes was excluded.";

                return Core::SushiException(Core::Error{Core::Errc::unsupported, std::move(detail)});
            }

            /**
             * @brief Discovers the devices that pass a filter and builds their queues.
             * @param provider Asked once for the hardware nodes.
             * @throws Core::SushiException With Errc::unsupported when no device passes.
             * @see modules/engine/execution/README.md
             */
            Device::DeviceQueueSet probe_devices(Topology::ITopologyProvider& provider,
                                                 const Topology::HardwareFilterConfig& config,
                                                 bool profiling)
            {
                try
                {
                    const std::vector<Topology::HardwareNode> nodes = provider.enumerate(config);
                    if (nodes.empty())
                        throw no_device_error(config);

                    std::vector<Device::DeviceDescriptor> descriptors;
                    descriptors.reserve(nodes.size());
                    for (const Topology::HardwareNode& node : nodes)
                        descriptors.push_back(describe_device(node));

                    return Device::DeviceQueueSet(std::move(descriptors), profiling);
                }
                catch (const std::exception& error)
                {
                    // The device set leaves a failed queue build unlogged; this is its one line.
                    SR_LOG_ERROR("Critical initialization failure: {}", error.what());
                    throw;
                }
            }
        } // namespace

        /**
         * @brief Holds the parts of a RuntimeContext in the order they are built.
         * @see modules/engine/execution/README.md
         */
        struct RuntimeContext::Impl : public Core::RefCounted
        {
            /**
             * @brief Builds every part but the engine, which needs the finished context.
             * @param provider Queried during construction only; not stored.
             * @param distance_model Must not be null.
             */
            Impl(const RuntimeSettings& runtime_settings,
                 const Topology::HardwareFilterConfig& config,
                 Topology::ITopologyProvider& provider,
                 std::shared_ptr<const Topology::INumaDistanceModel> distance_model)
                : settings(runtime_settings),
                  topology(provider, std::move(distance_model)),
                  devices(probe_devices(provider, config, settings.profiling)),
                  memory(std::make_shared<Memory::UsmAllocator>(devices.plain_queues()))
            {
            }

            /** @brief Stops everything that submits work, then lets the device set drain. */
            ~Impl()
            {
                stop_work();

                // The allocator can outlive the coordinator this object releases below.
                memory->set_release_observer(nullptr);

                // The engine is destroyed before devices drains (modules/engine/execution/README.md).
                engine.reset();

                // The scheduler holds the coordinator's address until its workers have joined.
                offload.reset();
            }

            /**
             * @brief Stops the installed coordinator, then the engine's worker pool.
             *
             * A second call stops nothing further: OffloadCoordinator::stop() may be called
             * again and the engine shuts down once.
             * @see modules/engine/execution/README.md
             */
            void stop_work()
            {
                // The coordinator returns its nodes to a pool that still runs.
                if (offload)
                    offload->stop();

                if (engine)
                    engine->shutdown();
            }

            Impl(const Impl&) = delete;
            Impl& operator=(const Impl&) = delete;

            /**
             * @brief Resolves the default provider and distance model, then builds an Impl.
             * @param provider Null selects HwlocTopologyProvider.
             * @param distance_model Null selects HwlocDistanceModel.
             */
            static sushi_ptr<Impl> create(const Topology::HardwareFilterConfig& config,
                                          std::shared_ptr<Topology::ITopologyProvider> provider,
                                          std::shared_ptr<Topology::INumaDistanceModel> distance_model,
                                          const RuntimeSettings& runtime_settings)
            {
                SR_LOG_INFO("Initializing compute stack...");

                if (!provider)
                    provider = std::make_shared<Topology::HwlocTopologyProvider>();
                if (!distance_model)
                    distance_model = std::make_shared<Topology::HwlocDistanceModel>();

                return make_sushi<Impl>(runtime_settings, config, *provider,
                                        std::move(distance_model));
            }

            /** @brief Runtime settings, fixed at construction (modules/engine/execution/README.md). */
            RuntimeSettings settings;

            /** @brief Core-to-NUMA map and NUMA distances. */
            Topology::TopologyView topology;

            /** @brief Device inventory and every SYCL queue. */
            Device::DeviceQueueSet devices;

            /** @brief USM allocator; shared because an allocation's owner may outlive the context. */
            std::shared_ptr<Memory::UsmAllocator> memory;

            /** @brief Byte counters of explicit transfers (modules/engine/execution/README.md). */
            Observability::TransferCounters transfers;

            /** @brief Installed offload coordinator, co-owned; null when there is none. */
            std::shared_ptr<Graph::OffloadCoordinator> offload;

            /** @brief Execution engine; built last, by RuntimeContext's constructor. */
            std::unique_ptr<ExecutionEngine> engine;
        };

        RuntimeContext::RuntimeContext(const Topology::HardwareFilterConfig& config,
                                       std::shared_ptr<Topology::ITopologyProvider> provider,
                                       std::shared_ptr<Topology::INumaDistanceModel> distance_model,
                                       const RuntimeSettings& settings)
            : impl_(Impl::create(config, std::move(provider), std::move(distance_model), settings))
        {
            // The engine reads this context while it is built, so impl_ is complete first.
            impl_->engine = std::make_unique<ExecutionEngine>(*this);
        }

        RuntimeContext::~RuntimeContext() = default;

        const RuntimeSettings& RuntimeContext::settings() const noexcept
        {
            return impl_->settings;
        }

        const Topology::TopologyView& RuntimeContext::topology() const noexcept
        {
            return impl_->topology;
        }

        Device::DeviceQueueSet& RuntimeContext::devices() noexcept
        {
            return impl_->devices;
        }

        Memory::UsmAllocator& RuntimeContext::memory() noexcept
        {
            return *impl_->memory;
        }

        const std::shared_ptr<Memory::UsmAllocator>& RuntimeContext::shared_memory() const noexcept
        {
            return impl_->memory;
        }

        Observability::TransferCounters& RuntimeContext::transfers() noexcept
        {
            return impl_->transfers;
        }

        IExecutionEngine& RuntimeContext::get_execution_engine()
        {
            SR_THROW_IF(!impl_->engine,
                        "ExecutionEngine is not initialized: the context is still being built.");
            return *impl_->engine;
        }

        void RuntimeContext::shutdown()
        {
            impl_->stop_work();
        }

        void RuntimeContext::install_offload(std::shared_ptr<Graph::OffloadCoordinator> offload)
        {
            SR_THROW_IF(!impl_->engine,
                        "ExecutionEngine is not initialized: the context is still being built.");
            impl_->engine->install_offload(offload.get());

            // The engine holds the new address before the replaced coordinator is released.
            impl_->offload = std::move(offload);
        }

        Graph::OffloadCoordinator* RuntimeContext::offload() const noexcept
        {
            return impl_->offload.get();
        }

        bool RuntimeContext::profiling_enabled() const noexcept
        {
            return impl_->settings.profiling;
        }

        void RuntimeContext::add_device_to_host_bytes(std::size_t bytes) noexcept
        {
            impl_->transfers.add_device_to_host(bytes);
        }

        void RuntimeContext::add_host_to_device_bytes(std::size_t bytes) noexcept
        {
            impl_->transfers.add_host_to_device(bytes);
        }

        std::size_t RuntimeContext::device_to_host_bytes() const noexcept
        {
            return impl_->transfers.device_to_host();
        }

        std::size_t RuntimeContext::host_to_device_bytes() const noexcept
        {
            return impl_->transfers.host_to_device();
        }

        sycl::queue& RuntimeContext::get_queue(size_t index)
        {
            return impl_->devices.queue(index);
        }

        sycl::queue& RuntimeContext::profiling_queue(size_t index)
        {
            return impl_->devices.queue(index, Device::QueueFlavour{false, true});
        }

        bool RuntimeContext::has_profiling_queue(size_t index) const noexcept
        {
            return impl_->devices.has_profiling_queue(index);
        }

        sycl::queue& RuntimeContext::chain_queue(size_t index, bool profiling)
        {
            return impl_->devices.queue(index, Device::QueueFlavour{true, profiling});
        }

        size_t RuntimeContext::device_count() const
        {
            return impl_->devices.count();
        }

        bool RuntimeContext::device_is_gpu(size_t index) const
        {
            return impl_->devices.is_gpu(index);
        }

        sycl::queue& RuntimeContext::get_numa_local_queue(int numa_id)
        {
            return impl_->devices.queue_or_numa_local(std::nullopt, numa_id);
        }

        sycl::queue& RuntimeContext::get_queue_for_ptr(void* ptr, int fallback_numa)
        {
            return impl_->devices.queue_or_numa_local(impl_->memory->owning_device(ptr),
                                                      fallback_numa);
        }

        int RuntimeContext::context_index_for_ptr(void* ptr) noexcept
        {
            const std::optional<std::size_t> device = impl_->memory->device_of(ptr);
            return device ? static_cast<int>(*device) : -1;
        }

        void* RuntimeContext::malloc_shared(size_t bytes, int numa_id)
        {
            return impl_->memory->allocate(
                {Memory::UsmKind::Shared, bytes, 0, impl_->devices.device_for_numa(numa_id)});
        }

        void* RuntimeContext::malloc_device(size_t bytes, int numa_id)
        {
            return impl_->memory->allocate(
                {Memory::UsmKind::Device, bytes, 0, impl_->devices.device_for_numa(numa_id)});
        }

        void* RuntimeContext::malloc_shared_on(size_t bytes, size_t device_index)
        {
            return impl_->memory->allocate({Memory::UsmKind::Shared, bytes, 0, device_index});
        }

        void* RuntimeContext::malloc_device_on(size_t bytes, size_t device_index)
        {
            return impl_->memory->allocate({Memory::UsmKind::Device, bytes, 0, device_index});
        }

        void* RuntimeContext::malloc_host_on(size_t bytes, size_t device_index)
        {
            return impl_->memory->allocate({Memory::UsmKind::Host, bytes, 0, device_index});
        }

        void RuntimeContext::free_usm(void* ptr)
        {
            impl_->memory->release(ptr);
        }

        void* RuntimeContext::aligned_malloc_host_on(size_t alignment, size_t bytes,
                                                     size_t device_index)
        {
            return impl_->memory->allocate({Memory::UsmKind::Host, bytes, alignment, device_index});
        }

        void* RuntimeContext::aligned_malloc_shared_on(size_t alignment, size_t bytes,
                                                       size_t device_index)
        {
            return impl_->memory->allocate(
                {Memory::UsmKind::Shared, bytes, alignment, device_index});
        }

        void* RuntimeContext::aligned_malloc_device_on(size_t alignment, size_t bytes,
                                                       size_t device_index)
        {
            return impl_->memory->allocate(
                {Memory::UsmKind::Device, bytes, alignment, device_index});
        }

        void RuntimeContext::adopt_usm(void* address, size_t device_index,
                                       std::optional<std::size_t> bytes)
        {
            impl_->memory->adopt(address, device_index, bytes);
        }

        std::optional<void*> RuntimeContext::containing_usm_base(void* ptr) const
        {
            return impl_->memory->containing_base(ptr);
        }

        void RuntimeContext::forget_usm(void* address) noexcept
        {
            impl_->memory->forget(address);
        }

        std::size_t RuntimeContext::live_usm_count() const
        {
            return impl_->memory->live_count();
        }

        int RuntimeContext::numa_distance(int numa_a, int numa_b) const
        {
            return impl_->topology.numa_distance(numa_a, numa_b);
        }

        const std::vector<int>& RuntimeContext::cpu_core_to_numa() const
        {
            return impl_->topology.core_to_numa();
        }

        void RuntimeContext::wait_all()
        {
            impl_->devices.wait_all();
        }

    } // namespace Execution
} // namespace SushiRuntime
