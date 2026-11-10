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
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file runtime_context.cpp
 * @brief Probes the hardware, builds the context's parts in order and tears them down.
 * @author Mustafa Garip
 * @see modules/engine/execution/README.md
 */

#include <SushiRuntime/execution/runtime_context.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/execution/execution_log_category.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/in_memory_queue_factory.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/sycl_queues.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/sycl_device_enumerator.hpp>
#include <SushiRuntime/topology/host_topology.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/refusing_offload_sink.hpp>
#include <SushiRuntime/execution/offload_host.hpp>
#include <SushiRuntime/execution/scheduler_execution_engine.hpp>

#include "host_execution_engine.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /**
             * @brief Converts a discovered hardware node to the descriptor a device set takes.
             * @param listed Every SYCL device, in the order the enumerator gave the node's id.
             */
            Internal::Device::DeviceDescriptor describe_device(const Internal::Topology::HardwareNode& node,
                                                               const std::vector<sycl::device>& listed)
            {
                Internal::Device::DeviceDescriptor descriptor;
                descriptor.device = listed.at(node.device_id);
                descriptor.info.name = node.name;
                descriptor.info.backend = descriptor.device.get_backend();
                descriptor.info.numa_node = node.numa_node_id;
                descriptor.info.memory_bytes = node.global_mem_size;
                descriptor.info.compute_units = node.max_compute_units;
                // The node's own flag names a GPU, so devices().is_gpu() answers as discovery did.
                descriptor.info.kind = node.is_gpu ? DeviceKind::GPU : node.kind;
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

                // The SYCL adapter maps each node's id back to its device.
                std::vector<sycl::device> listed;
                try
                {
                    listed = Internal::Topology::sycl_devices();
                }
                catch (const sycl::exception& error)
                {
                    return make_error(Error{ErrorCode::Unsupported, error.what()});
                }

                std::vector<Internal::Device::DeviceDescriptor> descriptors;
                descriptors.reserve(nodes->size());
                for (const Internal::Topology::HardwareNode& node : *nodes)
                {
                    if (node.device_id >= listed.size())
                        return make_error(Error{ErrorCode::Unsupported,
                                                "A discovered device has no SYCL device behind its id."});
                    descriptors.push_back(describe_device(node, listed));
                }
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
                 std::vector<Internal::Device::DeviceDescriptor> descriptors,
                 Profile::Collector& profile)
                : collector(profile),
                  settings(runtime_settings),
                  topology(provider, std::move(distance_model)),
                  devices(std::move(descriptors), false, profile),
                  memory(std::make_shared<Internal::Memory::USMAllocator>(
                      Internal::Memory::make_sycl_usm_source(
                          Internal::Memory::make_sycl_queues(devices.plain_queues())), profile))
            {
            }

            /**
             * @brief Builds every part of a host context but the engine: host devices whose
             *        queues, built on first use, are host queues of an InMemoryQueueFactory,
             *        and host USM.
             * @param provider Queried during construction only; not stored.
             * @param distance_model Must not be null.
             * @param host_devices The devices the context stands for; not empty.
             */
            Impl(const RuntimeSettings& runtime_settings,
                 Internal::Topology::TopologyProvider& provider,
                 std::shared_ptr<const Internal::Topology::NUMADistanceModel> distance_model,
                 std::vector<DeviceDescription> host_devices,
                 Profile::Collector& profile)
                : collector(profile),
                  settings(runtime_settings),
                  topology(provider, std::move(distance_model)),
                  devices(host_devices,
                          std::make_unique<Internal::Device::InMemoryQueueFactory>(host_devices.size()),
                          false, profile),
                  memory(std::make_shared<Internal::Memory::USMAllocator>(
                      Internal::Memory::make_host_usm_source(devices.count()), profile))
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

                // No run is left to report to them once the engine is gone.
                observers.clear();
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

            /** @brief The collector the context's zones, values and counters record into. */
            Profile::Collector& collector;

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

            /** @brief The engine as an OffloadHost, set at create; null for an engine that runs none. */
            OffloadHost* offload_host = nullptr;

            /** @brief The sink a context with no OffloadHost hands out; it refuses every node. */
            Internal::Graph::RefusingOffloadSink refusing_sink;

            /**
             * @brief Fills a record from the one in force, one generation above it, and
             *        publishes it.
             * @param next Not yet published; allocated by the caller, so a hold waits on none.
             * @param fill Called as fill(current, next), again when another publisher won.
             */
            template <class Fill>
            void publish(const std::shared_ptr<PlacementSnapshot>& next, Fill&& fill)
            {
                std::shared_ptr<const PlacementSnapshot> current = std::atomic_load(&placement);
                std::shared_ptr<const PlacementSnapshot> published = next;
                do
                {
                    fill(*current, *next);
                    next->generation = current->generation + 1;
                } while (!std::atomic_compare_exchange_strong(&placement, &current, published));
            }

            /**
             * @brief The record of the rules, the coordinator and their generation, swapped
             *        whole; never null.
             *
             * Read and written only through the std::atomic_* functions for shared_ptr.
             */
            std::shared_ptr<const PlacementSnapshot> placement =
                std::make_shared<const PlacementSnapshot>();

            /** @brief The observers added to this runtime. */
            RunObserverRegistry observers;

            /** @brief Facts of this machine, set once by set_machine_facts(); null before. */
            std::shared_ptr<const NodeFacts> machine;

            /** @brief Execution engine; built last, by RuntimeContext::create(). */
            std::unique_ptr<ExecutionEngine> engine;
        };

        Result<std::shared_ptr<RuntimeContext>>
        RuntimeContext::create(const DeviceFilter& config,
                               std::shared_ptr<Internal::Topology::TopologyProvider> provider,
                               std::shared_ptr<Internal::Topology::NUMADistanceModel> distance_model,
                               const RuntimeSettings& settings,
                               Profile::Collector& collector)
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
                    settings, *provider, std::move(distance_model), std::move(*descriptors), collector)));

                // The engine reads the context while it is built, so impl_ is complete first.
                auto engine = std::make_unique<SchedulerExecutionEngine>(*context);
                context->impl_->offload_host = engine.get();
                context->impl_->engine = std::move(engine);
            });
            if (!built)
                return initialization_failure(std::move(built.error()));
            if (refused)
                return initialization_failure(std::move(*refused));
            return context;
        }

        Result<std::shared_ptr<RuntimeContext>>
        RuntimeContext::create_host(std::size_t device_count, std::shared_ptr<Executor> executor,
                                    std::shared_ptr<const Clock> clock,
                                    std::shared_ptr<Internal::Topology::TopologyProvider> provider,
                                    std::shared_ptr<const Internal::Topology::NUMADistanceModel> distance_model,
                                    Profile::Collector& collector)
        {
            SR_VERIFY(device_count > 0,
                      "RuntimeContext::create_host: pass a device count of at least 1.");
            SR_VERIFY(executor != nullptr && clock != nullptr,
                      "RuntimeContext::create_host: pass an executor and a clock, not null.");
            RuntimeSettings settings;
            settings.worker_count = 1;
            settings.pinning = PinPolicy::Float;
            // Every run goes to the engine, since the chain submits to a device queue.
            settings.force_scheduler = true;

            Internal::Topology::HostTopology machine =
                Internal::Topology::make_host_topology(device_count);
            if (!provider)
                provider = std::move(machine.provider);
            if (!distance_model)
                distance_model = std::move(machine.distance_model);
            std::shared_ptr<RuntimeContext> context(new RuntimeContext(make_sushi<Impl>(
                settings, *provider, std::move(distance_model), std::move(machine.devices), collector)));
            const std::weak_ptr<RuntimeContext> observed = context;
            context->impl_->engine = std::make_unique<HostExecutionEngine>(
                std::move(executor), std::move(clock), device_count,
                [observed](const RunObservation& observation)
                {
                    if (const std::shared_ptr<RuntimeContext> held = observed.lock())
                        held->notify_run(observation);
                });
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

        Profile::Collector& RuntimeContext::profile_collector() const noexcept
        {
            return impl_->collector;
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
            SR_VERIFY(impl_->offload_host != nullptr || offload == nullptr,
                      "RuntimeContext::install_offload(): this context's engine runs no offload; "
                      "install a coordinator on a runtime that has a device.");
            if (impl_->offload_host != nullptr)
                impl_->offload_host->install_offload(offload.get());
            const bool installed = offload != nullptr;
            // The engine holds the new address before the replaced coordinator is released.
            impl_->offload = std::move(offload);
            impl_->publish(std::make_shared<PlacementSnapshot>(),
                           [installed](const PlacementSnapshot& current, PlacementSnapshot& next)
                           {
                               next.rules = current.rules;
                               next.offload_installed = installed;
                           });
        }

        std::uint64_t RuntimeContext::add_run_observer(std::shared_ptr<RunObserver> observer, TimingGate gate)
        {
            return impl_->observers.add(std::move(observer), gate);
        }

        void RuntimeContext::remove_run_observer(std::uint64_t id) noexcept
        {
            impl_->observers.remove(id);
        }

        bool RuntimeContext::wants_device_timing() const noexcept
        {
            return impl_->observers.wants_device_timing();
        }

        void RuntimeContext::notify_run(const RunObservation& observation) const noexcept
        {
            impl_->observers.notify(observation);
        }

        Internal::Graph::OffloadCoordinator* RuntimeContext::offload() const noexcept
        {
            return impl_->offload.get();
        }

        Internal::Graph::OffloadSink& RuntimeContext::offload_sink()
        {
            if (impl_->offload_host == nullptr)
                return impl_->refusing_sink;
            return impl_->offload_host->offload_sink();
        }

        bool RuntimeContext::set_placement_rules(std::shared_ptr<const PlacementRules> rules)
        {
            SR_VERIFY(impl_->engine != nullptr,
                      "ExecutionEngine is not initialized: the context is still being built.");
            // Allocated before the hold, so a run waiting on it waits for no allocation.
            const std::shared_ptr<PlacementSnapshot> next = std::make_shared<PlacementSnapshot>();
            next->rules = std::move(rules);
            // An engine with no OffloadHost holds nothing more than the run it is not running.
            OffloadHost* const host = impl_->offload_host;
            const bool held = host != nullptr ? host->begin_idle_change()
                                              : !impl_->engine->run_in_progress();
            if (!held)
                return false;
            impl_->publish(next, [](const PlacementSnapshot& current, PlacementSnapshot& filled)
                           { filled.offload_installed = current.offload_installed; });
            if (host != nullptr)
                host->end_idle_change();
            return true;
        }

        std::shared_ptr<const RuntimeContext::PlacementSnapshot>
        RuntimeContext::placement() const noexcept
        {
            return std::atomic_load(&impl_->placement);
        }

        void RuntimeContext::set_machine_facts(std::shared_ptr<const NodeFacts> facts)
        {
            SR_VERIFY(impl_->machine == nullptr,
                      "RuntimeContext::set_machine_facts: the facts of this machine are already "
                      "set; set them once, when the runtime is created.");
            impl_->machine = std::move(facts);
        }

        const NodeFacts* RuntimeContext::machine_facts() const noexcept
        {
            return impl_->machine.get();
        }

        RunObserverHandle observe_runs(const std::shared_ptr<RuntimeContext>& context,
                                       std::shared_ptr<RunObserver> observer, TimingGate gate)
        {
            SR_VERIFY(observer != nullptr, "observe_runs(): the observer is null.");
            const std::uint64_t id = context->add_run_observer(std::move(observer), gate);
            return RunObserverHandle([removable = std::weak_ptr<RuntimeContext>(context), id]()
            {
                if (const std::shared_ptr<RuntimeContext> held = removable.lock())
                    held->remove_run_observer(id);
            });
        }

    } // namespace Execution
} // namespace SushiRuntime
