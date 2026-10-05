/****************************************************************/
/* kernel_registry.cpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

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
