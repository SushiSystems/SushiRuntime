/**************************************************************************/
/* kernel_registry.hpp                                                    */
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
 * @file kernel_registry.hpp
 * @brief Declares KernelRegistry, which resolves an OpID to a pre-compiled kernel.
 * @author Mustafa Garip
 */

#pragma once

#include <vector>
#include <functional>
#include <unordered_map>
#include <SushiRuntime/core/span.hpp>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/task_types.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Resolves an OpID to a pre-compiled kernel for distributed execution.
         *
         * Each node maintains an identical local registry of kernels, so the network
         * only transmits IDs instead of compiled code.
         */
        class SUSHIRUNTIME_API KernelRegistry : public Core::RefCounted
        {
            public:
                /**
                 * @brief Defines the kernel signature that runs on network-resolved local buffers.
                 * @param inputs Local USM pointers providing read-only input data.
                 * @param outputs Local USM pointers providing writeable output data.
                 * @param meta Serialized scalar parameters of the task.
                 */
                using RemoteKernel = std::function<sycl::event(
                    sycl::queue&            queue,
                    SushiRuntime::span<void* const>  inputs,
                    SushiRuntime::span<void* const>  outputs,
                    const Graph::TaskMetadata& meta)>;

                /**
                 * @brief Registers or replaces a kernel implementation for a specific operation ID.
                 *
                 * Stores the callable inside the internal map using the operation ID as the key for later retrieval.
                 *
                 * @param id The unique identifier for the operation.
                 * @param fn The callable kernel implementation to register.
                 */
                void register_kernel(Graph::OpID id, RemoteKernel fn);

                /**
                 * @brief Checks whether a kernel implementation exists for the specified operation ID.
                 *
                 * Queries the internal registry map to confirm the presence of the operation.
                 *
                 * @param id The unique identifier for the operation to check.
                 * @return True if a kernel is registered, false otherwise.
                 */
                bool has(Graph::OpID id) const;

                /**
                 * @brief Retrieves the registered kernel implementation for a given operation ID.
                 *
                 * Looks up the ID in the internal registry and returns a pointer to the callable if found.
                 *
                 * @param id The unique identifier for the operation to find.
                 * @return A pointer to the kernel if found, or nullptr if the operation is unregistered.
                 */
                const RemoteKernel* find(Graph::OpID id) const;

                /**
                 * @brief Extracts a list of all currently registered operation IDs.
                 *
                 * Iterates through the internal registry and collects the keys into a flat list.
                 *
                 * @return A vector containing all registered operation IDs.
                 */
                std::vector<Graph::OpID> known_ops() const;

            private:
                std::unordered_map<uint32_t, RemoteKernel> kernels_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
