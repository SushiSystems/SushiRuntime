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
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds an array of shared memory and frees it on the queue it came from. */
    class SharedFloats
    {
        public:
            /**
             * @brief Allocates count floats of shared memory on a queue.
             * @param queue The queue the memory is shared with and freed on.
             * @param count The number of floats.
             */
            SharedFloats(sycl::queue& queue, std::size_t count)
                : queue_(queue), data_(sycl::malloc_shared<float>(count, queue))
            {
            }

            /** @brief Frees the memory on the queue it came from; a null allocation is left alone. */
            ~SharedFloats()
            {
                if (data_ != nullptr)
                {
                    sycl::free(data_, queue_);
                }
            }

            /** @brief Copying is not allowed, since the memory has one owner. */
            SharedFloats(const SharedFloats&) = delete;

            /** @brief Copy assignment is not allowed, since the memory has one owner. */
            SharedFloats& operator=(const SharedFloats&) = delete;

            /**
             * @brief Returns the first element.
             * @return Null when the allocation failed.
             */
            [[nodiscard]] float* data() const noexcept
            {
                return data_;
            }

        private:
            sycl::queue& queue_; /**< The queue the memory came from. */
            float* data_;        /**< The shared allocation. */
    };

    /**
     * @brief Writes the index of each element to a and 1 to b, so each sum is its index plus one.
     * @param a The first addends, count floats.
     * @param b The second addends, count floats.
     * @param count The number of elements.
     */
    void fill_addends(float* a, float* b, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            a[index] = static_cast<float>(index);
            b[index] = 1.0f;
        }
    }

    /**
     * @brief Asserts that each element of sum is its index plus one.
     * @param sum The sums, count floats.
     * @param count The number of elements.
     */
    void expect_index_plus_one(const float* sum, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            EXPECT_EQ(sum[index], static_cast<float>(index) + 1.0f) << "at " << index;
        }
    }
} // namespace

/** @brief Fixture of the medium tests of the distributed example, which submit work on the program's queue. */
class Medium_DistributedKernels : public TestSupport::RuntimeTest
{
};

/** @brief Tests that the kernel is registered under its operation. */
TEST(Small_DistributedKernels, TheKernelIsRegisteredUnderItsOperation)
{
    sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
    DistributedDemo::register_kernels(*kernels);
    EXPECT_TRUE(kernels->has(DistributedDemo::K_VECTOR_ADD));
}

/** @brief Tests that the kernel adds two vectors element by element on the program's queue. */
TEST_F(Medium_DistributedKernels, TheKernelAddsTwoVectors)
{
    constexpr std::size_t K_COUNT = 16;
    SharedFloats a(queue(), K_COUNT);
    SharedFloats b(queue(), K_COUNT);
    SharedFloats sum(queue(), K_COUNT);
    ASSERT_TRUE(a.data() != nullptr && b.data() != nullptr && sum.data() != nullptr);
    fill_addends(a.data(), b.data(), K_COUNT);
    const BufferView inputs[2] = {BufferView{a.data(), K_COUNT * sizeof(float)}, BufferView{b.data(), K_COUNT * sizeof(float)}};
    const BufferView outputs[1] = {BufferView{sum.data(), K_COUNT * sizeof(float)}};
    DistributedDemo::add_vectors(queue(), span<const BufferView>(inputs, 2), span<const BufferView>(outputs, 1), NodeOptions()).wait();
    expect_index_plus_one(sum.data(), K_COUNT);
}
