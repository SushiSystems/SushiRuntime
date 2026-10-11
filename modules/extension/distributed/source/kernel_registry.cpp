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

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/logging/logger.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        void KernelRegistry::register_kernel(Internal::Graph::OpID id, Kernel kernel)
        {
            if (!kernel)
                throw SushiException(Error{
                    Errc::invalid_access,
                    Detail::sr_format("KernelRegistry::register_kernel: the kernel of "
                                            "operation {} is empty; pass a callable.",
                                            id.value)});

            // Relaxed: the thread start that follows seal() publishes the flag with the map.
            SR_THROW_IF(sealed_.load(std::memory_order_relaxed),
                        "KernelRegistry::register_kernel: the registry is sealed, so operation "
                        "{} is not registered; register every kernel before the worker starts.",
                        id.value);

            kernels_[id.value] = std::move(kernel);
        }

        void KernelRegistry::seal() noexcept
        {
            sealed_.store(true, std::memory_order_relaxed);
        }

        bool KernelRegistry::is_sealed() const noexcept
        {
            return sealed_.load(std::memory_order_relaxed);
        }

        bool KernelRegistry::has(Internal::Graph::OpID id) const
        {
            return kernels_.find(id.value) != kernels_.end();
        }

        const KernelRegistry::Kernel* KernelRegistry::find(Internal::Graph::OpID id) const
        {
            const auto found = kernels_.find(id.value);
            return found == kernels_.end() ? nullptr : &found->second;
        }

        std::vector<Internal::Graph::OpID> KernelRegistry::known_ops() const
        {
            std::vector<Internal::Graph::OpID> ops;
            ops.reserve(kernels_.size());
            for (const auto& entry : kernels_)
                ops.push_back(Internal::Graph::OpID(entry.first));
            return ops;
        }

    } // namespace Distributed
} // namespace SushiRuntime
