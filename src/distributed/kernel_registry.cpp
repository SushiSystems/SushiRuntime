/**************************************************************************/
/* kernel_registry.cpp                                                    */
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

#include <SushiRuntime/distributed/kernel_registry.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Registers a remote kernel for a specific operation ID.
         * Stores the kernel function in the registry for future execution.
         * @param id The Graph::OpID identifying the operation.
         * @param fn The RemoteKernel function to register.
         */
        void KernelRegistry::register_kernel(Graph::OpID id, RemoteKernel fn)
        {
            kernels_[id.value] = std::move(fn);
        }

        /**
         * @brief Checks if a kernel is registered for the given operation ID.
         * Determines if the registry contains an entry for the specified ID.
         * @param id The Graph::OpID to check.
         * @return A boolean which is true if the kernel is registered, false otherwise.
         */
        bool KernelRegistry::has(Graph::OpID id) const
        {
            return kernels_.find(id.value) != kernels_.end();
        }

        /**
         * @brief Retrieves the remote kernel associated with an operation ID.
         * Looks up the ID and returns a pointer to the kernel function.
         * @param id The Graph::OpID to look up.
         * @return A pointer to the RemoteKernel if found, or nullptr otherwise.
         */
        const KernelRegistry::RemoteKernel* KernelRegistry::find(Graph::OpID id) const
        {
            auto it = kernels_.find(id.value);
            return it == kernels_.end() ? nullptr : &it->second;
        }

        /**
         * @brief Retrieves a list of all registered operation IDs.
         * Iterates through the registry and collects the IDs of all known kernels.
         * @return A vector of Graph::OpID elements containing all registered operations.
         */
        std::vector<Graph::OpID> KernelRegistry::known_ops() const
        {
            std::vector<Graph::OpID> ops;
            ops.reserve(kernels_.size());
            for (const auto& [value, _] : kernels_)
                ops.push_back(Graph::OpID(value));
            return ops;
        }

    } // namespace Distributed
} // namespace SushiRuntime
