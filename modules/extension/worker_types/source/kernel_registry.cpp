/**************************************************************************/
/* kernel_registry.cpp                                                    */
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
 * @file kernel_registry.cpp
 * @brief Implements KernelRegistry and the adapter for kernels that take bare addresses.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/kernel_registry.hpp>

#include <utility>

#include <SushiLog/text/text_format.hpp>

#include <SushiRuntime/core/assert.hpp>
#include <SushiRuntime/log/logger.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        void KernelRegistry::register_kernel(OperationID id, Kernel kernel)
        {
            SR_VERIFY(static_cast<bool>(kernel),
                      SushiLog::format_text("KernelRegistry::register_kernel: the kernel of "
                                            "operation {} is empty; pass a callable.",
                                            id.value)
                          .c_str());

            // Relaxed: the thread start that follows seal() publishes the flag with the map.
            SR_VERIFY(!sealed_.load(std::memory_order_relaxed),
                      SushiLog::format_text(
                          "KernelRegistry::register_kernel: the registry is sealed, so operation "
                          "{} is not registered; register every kernel before the worker starts.",
                          id.value)
                          .c_str());

            host_kernels_.erase(id.value);
            kernels_[id.value] = std::move(kernel);
        }

        void KernelRegistry::register_host_kernel(OperationID id, HostKernel kernel)
        {
            SR_VERIFY(static_cast<bool>(kernel),
                      SushiLog::format_text("KernelRegistry::register_host_kernel: the kernel of "
                                            "operation {} is empty; pass a callable.",
                                            id.value)
                          .c_str());
            SR_VERIFY(!sealed_.load(std::memory_order_relaxed),
                      SushiLog::format_text(
                          "KernelRegistry::register_host_kernel: the registry is sealed, so "
                          "operation {} is not registered; register every kernel before the worker "
                          "starts.",
                          id.value)
                          .c_str());

            kernels_[id.value] = [kernel](sycl::queue&, SushiRuntime::span<const BufferView> inputs,
                                          SushiRuntime::span<const BufferView> outputs,
                                          const NodeOptions& options)
            {
                kernel(inputs, outputs, options);
                return sycl::event{};
            };
            host_kernels_[id.value] = std::move(kernel);
        }

        void KernelRegistry::seal() noexcept
        {
            sealed_.store(true, std::memory_order_relaxed);
        }

        bool KernelRegistry::is_sealed() const noexcept
        {
            return sealed_.load(std::memory_order_relaxed);
        }

        bool KernelRegistry::has(OperationID id) const
        {
            return kernels_.find(id.value) != kernels_.end();
        }

        const KernelRegistry::Kernel* KernelRegistry::find(OperationID id) const
        {
            const auto found = kernels_.find(id.value);
            return found == kernels_.end() ? nullptr : &found->second;
        }

        const KernelRegistry::HostKernel* KernelRegistry::find_host(OperationID id) const
        {
            const auto found = host_kernels_.find(id.value);
            return found == host_kernels_.end() ? nullptr : &found->second;
        }

        std::vector<OperationID> KernelRegistry::known_ops() const
        {
            std::vector<OperationID> ops;
            ops.reserve(kernels_.size());
            for (const auto& entry : kernels_)
                ops.push_back(OperationID(entry.first));
            return ops;
        }

    } // namespace Distributed
} // namespace SushiRuntime
