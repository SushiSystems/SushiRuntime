/**************************************************************************/
/* runtime.hpp                                                            */
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
 * @file runtime.hpp
 * @brief Declares Runtime, the fluent entry point, and AdvancedControls.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/runtime/README.md
 */

#pragma once

#include <cstddef>
#include <memory>

#include <SushiRuntime/api/data/buffer.hpp>
#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/graph/graph.hpp>
#include <SushiRuntime/api/runtime/cluster.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/interop/interop.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Names the runtime-wide settings chosen once at Runtime::create().
         * @see include/SushiRuntime/api/runtime/README.md
         */
        using RuntimeConfig = Execution::RuntimeSettings;

        /** @brief Whether workers bind to cores; see Execution::PinPolicy. */
        using PinPolicy = Execution::PinPolicy;

        /**
         * @brief Exposes a narrow, read-only view of runtime-internal state.
         * @pre The view does not outlive the Runtime it came from.
         * @see include/SushiRuntime/api/runtime/README.md
         */
        class AdvancedControls
        {
            public:
                /**
                 * @brief Wraps the runtime's context for read-only inspection.
                 * @param ctx Context borrowed, not owned; must outlive this view.
                 */
                explicit AdvancedControls(Execution::RuntimeContext& ctx) noexcept
                    : ctx_(ctx)
                {
                }

                /**
                 * @brief Reports how many devices the runtime is driving.
                 * @return Number of device queues selected at construction; valid
                 *         device indices are [0, this).
                 */
                std::size_t device_count() const { return ctx_.device_count(); }

                /**
                 * @brief Tells whether per-kernel timings are being collected.
                 * @return True when the queues were built with profiling enabled.
                 */
                bool profiling_enabled() const noexcept { return ctx_.profiling_enabled(); }

                /**
                 * @brief Tells whether work is being redistributed by device load.
                 * @return True while the background thermal rebalancer is running.
                 */
                bool rebalancer_enabled() const
                {
                    return ctx_.get_execution_engine().rebalancer_enabled();
                }

                /**
                 * @brief Returns the worker pool as built, which may differ from the request.
                 * @return The worker count, first bound core, and pin policy in effect.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                Execution::WorkerPoolInfo worker_pool() const
                {
                    return ctx_.get_execution_engine().worker_pool();
                }

                /**
                 * @brief Reports upload traffic caused by explicit transfers.
                 * @return Cumulative bytes copied host to device since startup;
                 *         implicit shared-USM migration is not counted.
                 */
                std::size_t host_to_device_bytes() const noexcept
                {
                    return ctx_.host_to_device_bytes();
                }

                /**
                 * @brief Reports readback traffic caused by explicit transfers.
                 * @return Cumulative bytes copied device to host since startup;
                 *         implicit shared-USM migration is not counted.
                 */
                std::size_t device_to_host_bytes() const noexcept
                {
                    return ctx_.device_to_host_bytes();
                }

            private:
                Execution::RuntimeContext& ctx_;
        };

        /**
         * @brief Entry point of the fluent API: owns hardware and hands out graphs.
         *
         * Create one with Runtime::create(), then call graph() to build work.
         * The runtime owns the RuntimeContext (hardware, queues, engine); graphs
         * borrow it, so the Runtime must outlive every graph it produces.
         */
        class Runtime
        {
            public:
                /**
                 * @brief Creates a runtime with default device selection.
                 * @param config Device-class filter passed to hardware discovery.
                 * @param runtime_config Runtime-wide settings; defaults to the simulation profile.
                 * @throws SushiException If no device satisfies config.
                 */
                static Runtime create(const Topology::HardwareFilterConfig& config = {},
                                      const RuntimeConfig& runtime_config = {})
                {
                    return Runtime(std::make_shared<Execution::RuntimeContext>(
                        config, nullptr, nullptr, runtime_config));
                }

                /**
                 * @brief Starts a new, empty graph bound to this runtime.
                 * @return A fluent Graph; add tasks, then run() or try_run().
                 */
                Graph graph() { return Graph(ctx_); }

                /**
                 * @brief Starts a new dynamic, region-partitioned graph.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                DynamicGraph dynamic_graph();

                /**
                 * @brief Allocates a typed shared-USM buffer owned by this runtime.
                 * @pre The buffer does not outlive the Runtime.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                template <typename T>
                Buffer<T> buffer(std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                                 Residency residency = Residency::Shared)
                {
                    return Buffer<T>(ctx_, count, device_index, residency);
                }

                /**
                 * @brief Allocates a double-buffered, time-evolving State field.
                 * @pre The State does not outlive the Runtime.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                template <typename T>
                State<T> state(std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                               Residency residency = Residency::Shared)
                {
                    return State<T>(ctx_, count, device_index, residency);
                }

                /**
                 * @brief Enables or disables the background thermal rebalancer; safe to call
                 *        between runs.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                Runtime& rebalancer(bool enabled)
                {
                    ctx_->get_execution_engine().set_rebalancer_enabled(enabled);
                    return *this;
                }

                /**
                 * @brief A narrow, supported view of runtime-internal state.
                 * @return An AdvancedControls borrowing this runtime's context.
                 */
                AdvancedControls advanced() noexcept { return AdvancedControls(*ctx_); }

                /**
                 * @brief Provides the SYCL/Vulkan interop facade for this runtime's context.
                 * @return An Interop bound to this runtime's context.
                 */
                Interop interop() const noexcept { return Interop(ctx_); }

                /**
                 * @brief Returns the full runtime context, a non-public type that may break across
                 *        versions.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                Execution::RuntimeContext& unsafe_context() noexcept { return *ctx_; }

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Promotes this runtime to a distributed Master and returns its Cluster
                 *        handle.
                 * @param transport Connected master-side transport, one peer per worker.
                 * @param policy Offload policy; null selects LeastLoadedOffloadPolicy.
                 * @see include/SushiRuntime/api/runtime/README.md
                 */
                Cluster cluster(std::shared_ptr<Distributed::ITransport> transport,
                                sushi_ptr<Distributed::KernelRegistry> kernels,
                                std::shared_ptr<Distributed::IOffloadPolicy> policy = nullptr)
                {
                    ctx_->enable_distributed_master(std::move(transport),
                                                    std::move(kernels), std::move(policy));
                    return Cluster(ctx_);
                }
#endif // SR_DISTRIBUTED

            private:
                explicit Runtime(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;
        };

    } // namespace API
} // namespace SushiRuntime
