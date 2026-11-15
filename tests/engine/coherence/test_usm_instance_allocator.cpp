/**************************************************************************/
/* test_usm_instance_allocator.cpp                                        */
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
 * @file test_usm_instance_allocator.cpp
 * @brief Tests that USMInstanceAllocator makes an instance on the device its space names.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/coherence/usm_instance_allocator.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::USMAllocator;

namespace
{
    /** @brief Returns an allocator over host memory of two devices. */
    std::shared_ptr<USMAllocator> two_devices()
    {
        return std::make_shared<USMAllocator>(Internal::Memory::make_host_usm_source(2));
    }

    TEST(Small_USMInstanceAllocator, MakesTheInstanceOfASpaceOnItsDevice)
    {
        const std::shared_ptr<USMAllocator> memory = two_devices();
        USMInstanceAllocator subject(memory);

        void* const address = subject.allocate(MemorySpaceID(2), 64, 8);
        ASSERT_NE(address, nullptr);

        TestRunner::LogCapture capture;
        subject.release(MemorySpaceID(2), address);
        EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0U);
    }

    TEST(Small_USMInstanceAllocator, ReleasesHostMemoryOnTheDeviceItWasMadeOn)
    {
        USMInstanceAllocator subject(two_devices());
        void* const address = subject.allocate(MemorySpaceID(0), 64, 8);
        ASSERT_NE(address, nullptr);

        TestRunner::LogCapture capture;
        subject.release(MemorySpaceID(0), address);
        EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0U);
    }

    TEST(Small_USMInstanceAllocator, NamesDeviceUSMOnADeviceSpaceAndHostUSMOnTheHost)
    {
        const USMInstanceAllocator subject(two_devices());

        EXPECT_EQ(subject.kind_of(MemorySpaceID(1)), std::optional<Internal::Memory::USMKind>(Internal::Memory::USMKind::Device));
        EXPECT_EQ(subject.kind_of(MemorySpaceID(0)), std::optional<Internal::Memory::USMKind>(Internal::Memory::USMKind::Host));
    }

    TEST(Small_USMInstanceAllocator, RefusesASpacePastTheDevices)
    {
        USMInstanceAllocator subject(two_devices());

        EXPECT_EQ(subject.allocate(MemorySpaceID(3), 64, 8), nullptr);
    }
} // namespace
