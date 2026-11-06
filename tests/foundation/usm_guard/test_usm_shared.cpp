/**************************************************************************/
/* test_usm_shared.cpp                                                    */
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
 * @file test_usm_shared.cpp
 * @brief Tests USMShared: an allocation the host reads and writes, freed once, moved without a copy.
 * @see tests/foundation/usm_guard/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/usm_guard/usm_shared.hpp>

#include <support/runtime_environment.hpp>

using namespace SushiRuntime::Test;

namespace
{
    /** @brief Gives the USMShared tests the device queue of the program, and skips them on a machine with no device. */
    class Medium_USMShared : public TestSupport::RuntimeTest
    {
        protected:
            void SetUp() override
            {
                if (!has_any_sycl_device())
                    GTEST_SKIP() << "no SYCL device";
            }
    };

    /** @brief Writes @p factor times each element's position into @p values, from the host. */
    void write_multiples(USMShared<int>& values, int factor)
    {
        for (std::size_t index = 0; index < values.count(); ++index)
            values[index] = static_cast<int>(index) * factor;
    }

    /** @brief Returns what the host reads from every element of @p values. */
    std::vector<int> read_all(const USMShared<int>& values)
    {
        return std::vector<int>(values.get(), values.get() + values.count());
    }
} // namespace

/** @brief Tests that the host writes and reads every element. */
TEST_F(Medium_USMShared, TheHostWritesAndReadsEveryElement)
{
    USMShared<int> values(queue(), 6);
    ASSERT_TRUE(values);
    EXPECT_EQ(values.count(), 6u);

    write_multiples(values, 3);

    EXPECT_EQ(read_all(values), (std::vector<int>{0, 3, 6, 9, 12, 15}));
}

/** @brief Tests that a move construction hands the allocation over and empties the source. */
TEST_F(Medium_USMShared, AMoveConstructionHandsTheAllocationOver)
{
    USMShared<float> source(queue(), 4);
    float* const address = source.get();

    USMShared<float> target(std::move(source));

    EXPECT_FALSE(source);
    EXPECT_EQ(target.get(), address);
}

/** @brief Tests that a move assignment hands the allocation over, with its count. */
TEST_F(Medium_USMShared, AMoveAssignmentHandsTheAllocationOver)
{
    USMShared<float> source(queue(), 4);
    float* const address = source.get();
    USMShared<float> other(queue(), 2);

    other = std::move(source);

    EXPECT_EQ(other.get(), address);
    EXPECT_EQ(other.count(), 4u);
}
