/**************************************************************************/
/* test_kernel_registry.cpp                                               */
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
 * @file test_kernel_registry.cpp
 * @brief Tests KernelRegistry: sealing and the refusal of an empty kernel.
 * @see tests/extension/worker_types/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a kernel that submits nothing and returns a completed event. */
    KernelRegistry::Kernel idle_kernel()
    {
        return [](sycl::queue&, span<const BufferView>, span<const BufferView>,
                  const NodeOptions&) { return sycl::event{}; };
    }
}

/** @brief Tests that a sealed registry stops a registration, of a new operation or a known one. */
TEST(Medium_KernelRegistryDeathTest, SealedRegistryStopsARegistration)
{
    const OperationID early(21);
    const OperationID late(22);
    const sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    kernels->register_kernel(early, idle_kernel());
    kernels->seal();

    EXPECT_DEATH(kernels->register_kernel(late, idle_kernel()), "the registry is sealed");
    EXPECT_DEATH(kernels->register_kernel(early, idle_kernel()), "the registry is sealed");
}

/** @brief Tests that sealing marks the registry sealed and keeps the kernels registered before it. */
TEST(Small_KernelRegistry, SealingKeepsTheRegisteredKernels)
{
    const OperationID early(21);
    const sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    kernels->register_kernel(early, idle_kernel());
    EXPECT_FALSE(kernels->is_sealed());

    kernels->seal();

    EXPECT_TRUE(kernels->is_sealed());
    EXPECT_TRUE(kernels->has(early));
    EXPECT_EQ(kernels->known_ops().size(), 1u);
}

/** @brief Tests that a kernel that holds no callable stops the process, sealed or not. */
TEST(Medium_KernelRegistryDeathTest, EmptyKernelStopsTheRegistration)
{
    const OperationID operation(23);
    const sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();

    EXPECT_DEATH(kernels->register_kernel(operation, KernelRegistry::Kernel()),
                 "the kernel of operation 23 is empty");

    // An empty kernel is named as the fault before the seal is.
    kernels->seal();
    EXPECT_DEATH(kernels->register_kernel(operation, KernelRegistry::Kernel()),
                 "the kernel of operation 23 is empty");
}
