/**************************************************************************/
/* test_distributed_kernels.cpp                                           */
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
 * @file test_distributed_kernels.cpp
 * @brief Tests the distributed example's kernel: registered under its operation, it adds two vectors.
 * @see tests/applications/examples/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/distributed/distributed.hpp>

#include "distributed_kernels.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that the kernel is registered under its operation and adds element by element. */
TEST(Medium_DistributedKernels, TheKernelAddsTwoVectors)
{
    constexpr std::size_t K_COUNT = 16;
    sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    DistributedDemo::register_kernels(*kernels);
    EXPECT_TRUE(kernels->has(DistributedDemo::K_VECTOR_ADD));

    sycl::queue queue;
    float* const a = sycl::malloc_shared<float>(K_COUNT, queue);
    float* const b = sycl::malloc_shared<float>(K_COUNT, queue);
    float* const sum = sycl::malloc_shared<float>(K_COUNT, queue);
    ASSERT_TRUE(a != nullptr && b != nullptr && sum != nullptr);
    for (std::size_t index = 0; index < K_COUNT; ++index)
    {
        a[index] = static_cast<float>(index);
        b[index] = 1.0f;
    }
    const BufferView inputs[2] = {BufferView{a, K_COUNT * sizeof(float)}, BufferView{b, K_COUNT * sizeof(float)}};
    const BufferView outputs[1] = {BufferView{sum, K_COUNT * sizeof(float)}};
    DistributedDemo::add_vectors(queue, span<const BufferView>(inputs, 2), span<const BufferView>(outputs, 1), NodeOptions()).wait();
    for (std::size_t index = 0; index < K_COUNT; ++index)
        EXPECT_EQ(sum[index], static_cast<float>(index) + 1.0f) << "at " << index;
    sycl::free(a, queue);
    sycl::free(b, queue);
    sycl::free(sum, queue);
}
