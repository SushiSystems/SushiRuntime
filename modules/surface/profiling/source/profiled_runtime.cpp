/**************************************************************************/
/* profiled_runtime.cpp                                                   */
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
 * @file profiled_runtime.cpp
 * @brief Implements profiled_runtime_of, which reads a Runtime's context for the profiler registration.
 * @author Mustafa Garip
 * @see modules/surface/profiling/README.md
 */

#include <SushiRuntime/profiling/profiled_runtime.hpp>

#include <utility>

#include <sycl/sycl.hpp>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Reads one runtime's context. */
        class ContextProfiledRuntime final : public ProfiledRuntime
        {
            public:
                /** @brief Builds the reader over @p context, which it keeps alive. */
                explicit ContextProfiledRuntime(std::shared_ptr<Internal::Execution::RuntimeContext> context) noexcept
                    : context_(std::move(context))
                {
                }

                [[nodiscard]] std::string node_name() const override
                {
                    const NodeFacts* node = context_->machine_facts();
                    return node != nullptr ? node->name : std::string("node");
                }

                [[nodiscard]] std::vector<DeviceDescription> devices() const override
                {
                    return context_->devices().devices();
                }

                [[nodiscard]] std::string driver_of(std::size_t device) const override
                {
                    // A host device has no SYCL device behind it to ask.
                    if (!context_->devices().holds_queues())
                        return "unknown";
                    try
                    {
                        return context_->devices().queue(device).get_device()
                            .get_info<sycl::info::device::driver_version>();
                    }
                    catch (const sycl::exception&)
                    {
                        return "unknown";
                    }
                }

                [[nodiscard]] std::size_t worker_count() const override
                {
                    return context_->execution_engine().worker_pool().count;
                }

                [[nodiscard]] bool rebalancer_enabled() const override
                {
                    return context_->execution_engine().rebalancer_enabled();
                }

                [[nodiscard]] RunObserverHandle observe_runs(std::shared_ptr<RunObserver> observer,
                                                             WantedGate wanted) override
                {
                    return Internal::Execution::observe_runs(context_, std::move(observer), wanted);
                }

            private:
                /** @brief The runtime's context. */
                std::shared_ptr<Internal::Execution::RuntimeContext> context_;
        };
    } // namespace

    std::shared_ptr<ProfiledRuntime> profiled_runtime_of(const Runtime& runtime)
    {
        return std::make_shared<ContextProfiledRuntime>(Internal::ContextAccess::shared(runtime));
    }
} // namespace SushiRuntime
