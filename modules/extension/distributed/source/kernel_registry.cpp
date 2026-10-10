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
 * @file kernel_registry.cpp
 * @brief Implements KernelRegistry and the adapter for kernels that take bare addresses.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/kernel_registry.hpp>

#include <utility>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Returns the addresses of a list of views, in the same order. */
            std::vector<void*> addresses_of(SushiRuntime::span<const BufferView> views)
            {
                std::vector<void*> addresses;
                addresses.reserve(views.size());
                for (const BufferView& view : views)
                    addresses.push_back(view.data);
                return addresses;
            }
        } // namespace

        void KernelRegistry::register_kernel(Graph::OpID id, Kernel kernel)
        {
            kernels_[id.value] = std::move(kernel);
        }

        void KernelRegistry::register_kernel(Graph::OpID id, RemoteKernel kernel)
        {
            Kernel adapter = [legacy = std::move(kernel)](
                                 sycl::queue& queue, SushiRuntime::span<const BufferView> inputs,
                                 SushiRuntime::span<const BufferView> outputs,
                                 const Graph::TaskMetadata& metadata) -> sycl::event
            {
                const std::vector<void*> input_addresses = addresses_of(inputs);
                const std::vector<void*> output_addresses = addresses_of(outputs);
                return legacy(
                    queue,
                    SushiRuntime::span<void* const>(input_addresses.data(),
                                                    input_addresses.size()),
                    SushiRuntime::span<void* const>(output_addresses.data(),
                                                    output_addresses.size()),
                    metadata);
            };
            kernels_[id.value] = std::move(adapter);
        }

        bool KernelRegistry::has(Graph::OpID id) const
        {
            return kernels_.find(id.value) != kernels_.end();
        }

        const KernelRegistry::Kernel* KernelRegistry::find(Graph::OpID id) const
        {
            const auto found = kernels_.find(id.value);
            return found == kernels_.end() ? nullptr : &found->second;
        }

        std::vector<Graph::OpID> KernelRegistry::known_ops() const
        {
            std::vector<Graph::OpID> ops;
            ops.reserve(kernels_.size());
            for (const auto& entry : kernels_)
                ops.push_back(Graph::OpID(entry.first));
            return ops;
        }

    } // namespace Distributed
} // namespace SushiRuntime
