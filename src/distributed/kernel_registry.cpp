/**************************************************************************/
/* kernel_registry.cpp                                                    */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
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
