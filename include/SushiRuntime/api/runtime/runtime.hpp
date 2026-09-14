/**************************************************************************/
/* runtime.hpp                                                            */
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
         * @brief Optional runtime-wide settings chosen once at Runtime::create().
         *
         * The co-tenancy contract: worker count, core placement, pinning policy,
         * profiling, and whether the background rebalancer runs. A runtime inside a
         * game shares the machine with a render thread, an audio callback and asset
         * I/O, so every one of these defaults is chosen for a tenant rather than an
         * owner. See Execution::RuntimeSettings for the per-field documentation.
         *
         * This is an alias, not a distinct type: RuntimeContext and TaskScheduler
         * consume the same settings and live below this layer, which may not depend
         * on it. RuntimeConfig is the public spelling; RuntimeSettings is where it
         * lives.
         */
        using RuntimeConfig = Execution::RuntimeSettings;

        /** @brief Whether workers bind to cores; see Execution::PinPolicy. */
        using PinPolicy = Execution::PinPolicy;

        /**
         * @brief A narrow, read-only view of runtime-internal state.
         *
         * The supported way to read the few internal facts a caller legitimately
         * needs — device count, profiling/rebalancer state, cumulative transfer
         * bytes — without exposing the whole RuntimeContext. Obtained from
         * Runtime::advanced(); it borrows the context, so it must not outlive the
         * Runtime. For anything beyond this surface, Runtime::unsafe_context() is the
         * deliberate escape hatch.
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
                 * @brief The worker pool as built, which may differ from the request.
                 *
                 * A caller that reserved cores for its own threads verifies here that
                 * the reservation took effect: a range that did not fit the machine is
                 * clamped, and this reports the clamped result rather than the request.
                 *
                 * @return The worker count, first bound core, and pin policy in effect.
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
                 * @param config         Device-class filter passed to hardware discovery.
                 * @param runtime_config Runtime-wide settings — workers, core placement,
                 *                        profiling, rebalancer; defaults to the
                 *                        simulation profile.
                 * @return A ready Runtime.
                 * @throws SushiException If no device satisfies @p config.
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
                 *
                 * The mutable counterpart to graph(): work is grouped into regions
                 * (region(key)) that can be added or dropped between steps, so a
                 * streaming open world re-stitches only the cells that changed
                 * instead of recompiling the whole graph. Defined out of line in
                 * dynamic_graph.hpp once DynamicGraph is a complete type.
                 *
                 * @return A fluent DynamicGraph; populate regions, then run().
                 */
                DynamicGraph dynamic_graph();

                /**
                 * @brief Allocates a typed shared-USM buffer owned by this runtime.
                 *
                 * The buffer borrows the runtime's context for allocation and
                 * frees itself on destruction, so it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements.
                 * @param device_index Target device queue; selects the owning context.
                 * @param residency    Shared (host-addressable) or Device (resident).
                 * @return An owning Buffer<T> handle.
                 */
                template <typename T>
                Buffer<T> buffer(std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                                 Residency residency = Residency::Shared)
                {
                    return Buffer<T>(ctx_, count, device_index, residency);
                }

                /**
                 * @brief Allocates a double-buffered, time-evolving State field.
                 *
                 * Use this for data a stepped graph reads as "current" and writes
                 * as "next"; the runtime swaps the two each step. Like buffer(),
                 * it must not outlive the Runtime.
                 *
                 * @tparam T            Element type (trivially copyable for device use).
                 * @param count        Number of elements in each of the two buffers.
                 * @param device_index Target device queue for both buffers.
                 * @param residency    Shared (host-addressable) or Device (resident).
                 * @return An owning State<T> handle.
                 */
                template <typename T>
                State<T> state(std::size_t count, DeviceIndex device_index = DeviceIndex{0},
                               Residency residency = Residency::Shared)
                {
                    return State<T>(ctx_, count, device_index, residency);
                }

                /**
                 * @brief Enables or disables the background thermal rebalancer.
                 *
                 * Off unless RuntimeConfig::rebalancer asked for it. Running it
                 * trades run-to-run reproducibility and the jitter floor for
                 * throughput, by migrating tasks between workers on a 5 ms
                 * heartbeat — worth it for long batch work, wrong for a simulation
                 * frame. Safe to call between runs.
                 *
                 * @param enabled True to run the rebalancer, false to stop it.
                 * @return *this, for chaining.
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
                 * @brief The full runtime context — the deliberate escape hatch.
                 *
                 * Exposes the internal RuntimeContext for uses the fluent surface and
                 * advanced() do not cover. The name is a warning: code that reaches
                 * here couples to a non-public type and may break across versions.
                 * Prefer the fluent API or advanced().
                 *
                 * @return A reference to the underlying context, owned by this Runtime.
                 */
                Execution::RuntimeContext& unsafe_context() noexcept { return *ctx_; }

#ifdef SR_DISTRIBUTED
                /**
                 * @brief Promotes this runtime to a distributed Master and returns
                 *        a Cluster handle governing it.
                 *
                 * Thin fluent wrapper over RuntimeContext::enable_distributed_master:
                 * wires the transport and starts serving. Until called, the runtime
                 * is a fully offline single machine (zero distributed overhead).
                 *
                 * @param transport Connected master-side transport (one peer per worker).
                 * @param kernels   Registry shared with this binary's kernels.
                 * @param policy    Offload policy (null → LeastLoadedOffloadPolicy).
                 * @return A Cluster handle for tuning and telemetry.
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
