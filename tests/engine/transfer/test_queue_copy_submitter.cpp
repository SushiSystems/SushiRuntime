/**************************************************************************/
/* test_queue_copy_submitter.cpp                                          */
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
 * @file test_queue_copy_submitter.cpp
 * @brief Tests that QueueCopySubmitter copies the bytes on the queue after its dependencies.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/transfer/queue_copy_submitter.hpp>

#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Medium_QueueCopySubmitter, CopiesTheBytesAfterAnEarlierCopyItDependsOn)
    {
        CPUSpaceQueues queues;
        if (!queues.available())
            GTEST_SKIP() << "SYCL offers no CPU device.";
        sycl::queue& queue = *queues.queue_of(SushiRuntime::MemorySpaceID(1));
        const std::vector<std::uint8_t> source{1, 2, 3, 4, 5};
        std::vector<std::uint8_t> middle(source.size(), 0);
        std::vector<std::uint8_t> target(source.size(), 0);
        QueueCopySubmitter subject;

        const sycl::event first = subject.submit(queue, middle.data(), source.data(), source.size(), {});
        sycl::event second = subject.submit(queue, target.data(), middle.data(), middle.size(), {first});
        second.wait();

        EXPECT_EQ(target, source);
    }
} // namespace
