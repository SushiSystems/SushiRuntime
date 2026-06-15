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
        void KernelRegistry::register_kernel(Graph::OpID id, RemoteKernel fn)
        {
            kernels_[id.value] = std::move(fn);
        }

        bool KernelRegistry::has(Graph::OpID id) const
        {
            return kernels_.find(id.value) != kernels_.end();
        }

        const KernelRegistry::RemoteKernel* KernelRegistry::find(Graph::OpID id) const
        {
            auto it = kernels_.find(id.value);
            return it == kernels_.end() ? nullptr : &it->second;
        }

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
