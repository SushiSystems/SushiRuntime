/**************************************************************************/
/* kernel_registry.hpp                                                    */
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

#include <vector>
#include <functional>
#include <unordered_map>
#include <span>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/graph/task_types.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Maps an OpID to a kernel already compiled into this binary.
         *
         * This is the answer to "how do you distribute a compiled project": you
         * do not ship code, you ship an OpID, and every node looks that id up in
         * its own identical registry. The master uses it (via the common set
         * negotiated at HELLO) to decide which tasks are even offloadable; each
         * worker uses it to actually run the work.
         *
         * Both ends register the SAME kernels at startup — they are part of the
         * same binary. A task whose op_id is absent here simply stays local.
         */
        class SUSHIRUNTIME_API KernelRegistry : public Core::RefCounted
        {
            public:
                /**
                 * @brief A kernel callable in terms of network-resolved local buffers.
                 *
                 * inputs/outputs are local USM pointers the worker resolved from
                 * BufferHandles; meta carries the serialized scalar params.
                 */
                using RemoteKernel = std::function<sycl::event(
                    sycl::queue&            queue,
                    std::span<void* const>  inputs,
                    std::span<void* const>  outputs,
                    const Graph::TaskMetadata& meta)>;

                /** @brief Registers (or replaces) the kernel for an OpID. */
                void register_kernel(Graph::OpID id, RemoteKernel fn);

                /** @brief True if a kernel is registered for this OpID. */
                bool has(Graph::OpID id) const;

                /** @brief Returns the kernel for an OpID, or nullptr. */
                const RemoteKernel* find(Graph::OpID id) const;

                /** @brief All registered OpIDs, sent in the HELLO handshake. */
                std::vector<Graph::OpID> known_ops() const;

            private:
                std::unordered_map<uint32_t, RemoteKernel> kernels_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
