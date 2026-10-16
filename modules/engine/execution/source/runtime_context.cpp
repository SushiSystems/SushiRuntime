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

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/logging/logger.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/execution/scheduler_execution_engine.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Converts a discovered hardware node to the descriptor a device set takes. */
            Internal::Device::DeviceDescriptor describe_device(const Internal::Topology::HardwareNode& node)
            {
                Internal::Device::DeviceDescriptor descriptor;
                descriptor.device = node.sycl_dev;
                descriptor.info.name = node.name;
                descriptor.info.backend = node.sycl_dev.get_backend();
                descriptor.info.numa_node = node.numa_node_id;
                descriptor.info.memory_bytes = node.global_mem_size;
                descriptor.info.compute_units = node.max_compute_units;

                // The node's own flag names a GPU, so devices().is_gpu() answers as discovery did.
                if (node.is_gpu)
                    descriptor.info.kind = DeviceKind::GPU;
                else if (node.sycl_dev.is_cpu())
                    descriptor.info.kind = DeviceKind::CPU;
                else if (node.sycl_dev.is_accelerator())
                    descriptor.info.kind = DeviceKind::Accelerator;
                else
                    descriptor.info.kind = DeviceKind::Other;

                return descriptor;
            }

            /** @brief Builds the error of a filter that no SYCL device passed. */
            Error no_device_error(const DeviceFilter& config)
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

                return Error{ErrorCode::Unsupported, std::move(detail)};
            }

            /**
             * @brief Discovers the devices that pass a filter and describes them.
             * @param provider Asked once for the hardware nodes.
             * @return The descriptors, or Unsupported when the devices cannot be listed or none
             *         passes.
             * @see modules/engine/execution/README.md
             */
            Result<std::vector<Internal::Device::DeviceDescriptor>>
            probe_devices(Internal::Topology::TopologyProvider& provider,
                          const DeviceFilter& config)
            {
                Result<std::vector<Internal::Topology::HardwareNode>> nodes =
                    provider.enumerate(config);
                if (!nodes)
                    return make_error(std::move(nodes.error()));
                if (nodes->empty())
                    return make_error(no_device_error(config));

                std::vector<Internal::Device::DeviceDescriptor> descriptors;
                descriptors.reserve(nodes->size());
                for (const Internal::Topology::HardwareNode& node : *nodes)
                    descriptors.push_back(describe_device(node));
                return descriptors;
            }

            /** @brief Logs why no context could be built and returns the reason as an error. */
            ErrorWrapper<Error> initialization_failure(Error reason)
            {
                // The device set leaves a failed queue build unlogged; this is its one line.
                SR_LOG_ERROR(K_EXECUTION_LOG_CATEGORY, "Critical initialization failure: {}", reason.message);
                return make_error(std::move(reason));
            }
        } // namespace

        /**
         * @brief Holds the parts of a RuntimeContext in the order they are built.
         * @see modules/engine/execution/README.md
         */
        struct RuntimeContext::Impl : public RefCounted
        {
            /**
             * @brief Builds every part but the engine, which needs the finished context.
             * @param provider Queried during construction only; not stored.
             * @param distance_model Must not be null.
             * @param descriptors The devices the context runs on; not empty.
             */
            Impl(const RuntimeSettings& runtime_settings,
                 Internal::Topology::TopologyProvider& provider,
                 std::shared_ptr<const Internal::Topology::NUMADistanceModel> distance_model,
                 std::vector<Internal::Device::DeviceDescriptor> descriptors)
                : settings(runtime_settings),
                  topology(provider, std::move(distance_model)),
                  devices(std::move(descriptors), settings.profiling),
                  memory(std::make_shared<Internal::Memory::USMAllocator>(devices.plain_queues()))
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

            /** @brief Runtime settings, fixed at construction (modules/engine/execution/README.md). */
            RuntimeSettings settings;

            /** @brief Core-to-NUMA map and NUMA distances. */
            Internal::Topology::TopologyView topology;

            /** @brief Device inventory and every SYCL queue. */
            Internal::Device::DeviceQueueSet devices;

            /** @brief USM allocator; shared because an allocation's owner may outlive the context. */
            std::shared_ptr<Internal::Memory::USMAllocator> memory;

            /** @brief Byte counters of explicit transfers (modules/engine/execution/README.md). */
            Internal::Observability::TransferCounters transfers;

            /** @brief Installed offload coordinator, co-owned; null when there is none. */
            std::shared_ptr<Internal::Graph::OffloadCoordinator> offload;

            /** @brief Execution engine; built last, by RuntimeContext::create(). */
            std::unique_ptr<SchedulerExecutionEngine> engine;
        };

        Result<std::shared_ptr<RuntimeContext>>
        RuntimeContext::create(const DeviceFilter& config,
                               std::shared_ptr<Internal::Topology::TopologyProvider> provider,
                               std::shared_ptr<Internal::Topology::NUMADistanceModel> distance_model,
                               const RuntimeSettings& settings)
        {
            SR_LOG_INFO(K_EXECUTION_LOG_CATEGORY, "Initializing compute stack...");

            std::shared_ptr<RuntimeContext> context;
            std::optional<Error> refused;

            // SYCL and the standard library throw from the constructors inside.
            Result<void> built = SYCL::translate_exceptions(
                ErrorCode::DeviceLost, ErrorCode::AllocationFailed, [&]()
            {
                if (!provider)
                    provider = std::make_shared<Internal::Topology::HwlocTopologyProvider>();
                if (!distance_model)
                    distance_model = std::make_shared<Internal::Topology::HwlocDistanceModel>();

                Result<std::vector<Internal::Device::DeviceDescriptor>> descriptors =
                    probe_devices(*provider, config);
                if (!descriptors)
                {
                    refused = std::move(descriptors.error());
                    return;
                }

                context = std::shared_ptr<RuntimeContext>(new RuntimeContext(make_sushi<Impl>(
                    settings, *provider, std::move(distance_model), std::move(*descriptors))));

                // The engine reads the context while it is built, so impl_ is complete first.
                context->impl_->engine = std::make_unique<SchedulerExecutionEngine>(*context);
            });
            if (!built)
                return initialization_failure(std::move(built.error()));
            if (refused)
                return initialization_failure(std::move(*refused));
            return context;
        }

        RuntimeContext::RuntimeContext(sushi_ptr<Impl> impl) noexcept : impl_(std::move(impl))
        {

        }

        RuntimeContext::~RuntimeContext() = default;

        const RuntimeSettings& RuntimeContext::settings() const noexcept
        {
            return impl_->settings;
        }

        const Internal::Topology::TopologyView& RuntimeContext::topology() const noexcept
        {
            return impl_->topology;
        }

        Internal::Device::DeviceQueueSet& RuntimeContext::devices() noexcept
        {
            return impl_->devices;
        }

        Internal::Memory::USMAllocator& RuntimeContext::memory() noexcept
        {
            return *impl_->memory;
        }

        const std::shared_ptr<Internal::Memory::USMAllocator>& RuntimeContext::shared_memory() const noexcept
        {
            return impl_->memory;
        }

        Internal::Observability::TransferCounters& RuntimeContext::transfers() noexcept
        {
            return impl_->transfers;
        }

        ExecutionEngine& RuntimeContext::execution_engine()
        {
            SR_VERIFY(impl_->engine != nullptr,
                      "ExecutionEngine is not initialized: the context is still being built.");
            return *impl_->engine;
        }

        void RuntimeContext::shutdown()
        {
            impl_->stop_work();
        }

        void RuntimeContext::install_offload(std::shared_ptr<Internal::Graph::OffloadCoordinator> offload)
        {
            SR_VERIFY(impl_->engine != nullptr,
                      "ExecutionEngine is not initialized: the context is still being built.");
            impl_->engine->install_offload(offload.get());

            // The engine holds the new address before the replaced coordinator is released.
            impl_->offload = std::move(offload);
        }

        Internal::Graph::OffloadCoordinator* RuntimeContext::offload() const noexcept
        {
            return impl_->offload.get();
        }

        Internal::Graph::OffloadSink& RuntimeContext::offload_sink()
        {
            SR_VERIFY(impl_->engine != nullptr,
                      "ExecutionEngine is not initialized: the context is still being built.");
            return impl_->engine->offload_sink();
        }

    } // namespace Execution
} // namespace SushiRuntime
