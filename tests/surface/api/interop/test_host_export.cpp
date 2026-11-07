/**************************************************************************/
/* test_host_export.cpp                                                   */
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
 * @file test_host_export.cpp
 * @brief Tests exporting host memory a device and the host both reach.
 * @see tests/surface/api/interop/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the multiples of three below 3 * @p count, the values a kernel writes by index. */
    std::vector<std::uint32_t> multiples_of_three(std::size_t count)
    {
        std::vector<std::uint32_t> values = TestSupport::ramp<std::uint32_t>(count);
        for (std::uint32_t& value : values)
            value *= 3u;
        return values;
    }
} // namespace

/** @brief Fixture of the host export tests that need a device, which run on the program's runtime. */
class Medium_InteropHostExport : public TestSupport::RuntimeTest
{
};

/** @brief Fixture of the host export tests that need no kernel, which run on a host runtime. */
class Small_InteropHostExport : public TestSupport::HostRuntimeTest
{
};

/** @brief Fixture of the host export tests that stop the process. */
class Medium_InteropHostExportDeathTest : public TestSupport::RuntimeTest
{
};

/** @brief Fixture of the import test that needs a device without the import extension. */
class Medium_InteropImportRefusal : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Skips the test when device 0 imports external memory, since there is no refusal to see. */
        void SetUp() override
        {
            if (runtime().interop().capabilities(DeviceIndex{0}).value().device_import)
                GTEST_SKIP() << "device 0 imports external memory; this case covers the refusal only";
        }
};

/** @brief Tests that an exported host region is aligned and rounded to the requested alignment. */
TEST_F(Medium_InteropHostExport, AlignsAndRoundsTheRegion)
{
    auto shared = runtime().interop().export_host<std::uint32_t>(1000, DeviceIndex{0}, 4096).value();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shared.address()) % 4096u, 0u);
    EXPECT_EQ(shared.bytes % 4096u, 0u);
    EXPECT_GE(shared.bytes, 1000u * sizeof(std::uint32_t));
    EXPECT_EQ(static_cast<void*>(shared.buffer.data()), shared.address());
}

/** @brief Tests that a range written to an exported buffer reads back, routed to the device that owns it. */
TEST_F(Small_InteropHostExport, ARangeWrittenToAnExportReadsBack)
{
    auto shared = runtime().interop().export_host<float>(16, DeviceIndex{0}, 4096).value();
    const std::vector<float> written = TestSupport::ramp<float>(16, 0.5f);

    ASSERT_TRUE(shared.buffer.write_range(ElementRange{0, 16}, span<const float>(written)));
    const auto read = shared.buffer.read_range(ElementRange{0, 16});

    ASSERT_TRUE(read);
    EXPECT_EQ(*read, written);
}

/** @brief Tests that a kernel writing through the exported buffer is visible from the host. */
TEST_F(Medium_InteropHostExport, AKernelWritesWhatTheHostReads)
{
    auto shared = runtime().interop().export_host<std::uint32_t>(257, DeviceIndex{0}, 4096).value();
    std::uint32_t* out = shared.buffer.data();

    runtime().graph()
        .add(257, Reads(), Writes(shared.buffer),
             [out](std::size_t i) { out[i] = static_cast<std::uint32_t>(i * 3u); })
        .run().value();

    EXPECT_EQ(TestSupport::contents(shared.buffer), multiples_of_three(257));
}

/** @brief Tests that importing on a device without the import extension returns Unsupported. */
TEST_F(Medium_InteropImportRefusal, ImportOnADeviceWithoutTheExtensionReturnsUnsupported)
{
    ExternalMemoryDescriptor descriptor;

    const auto imported = runtime().interop().import_device<float>(descriptor, 4, DeviceIndex{0});

    ASSERT_FALSE(imported.has_value()) << "import_device returned on a device without import";
    EXPECT_EQ(imported.error().code, ErrorCode::Unsupported);
}

/** @brief Tests that an alignment that is not a power of two stops the process. */
TEST_F(Medium_InteropHostExportDeathTest, AnAlignmentThatIsNotAPowerOfTwoStops)
{
    EXPECT_DEATH(
        static_cast<void>(runtime().interop().export_host<float>(16, DeviceIndex{0}, 4097)),
        "export_host: alignment 4097 is not a power of two");
}

/** @brief Tests that a byte size that overflows when rounded up to the alignment stops the process. */
TEST_F(Medium_InteropHostExportDeathTest, ASizeThatOverflowsWhenRoundedStops)
{
    const std::size_t most = std::numeric_limits<std::size_t>::max();

    EXPECT_DEATH(
        static_cast<void>(runtime().interop().export_host<std::uint8_t>(most, DeviceIndex{0}, 4096)),
        "bytes overflow std::size_t when rounded up to the alignment of 4096");
}

/** @brief Tests that an export no device can hold returns AllocationFailed. */
TEST_F(Small_InteropHostExport, AnExportNoDeviceCanSatisfyReturnsAllocationFailed)
{
    // Half the address space in bytes: it rounds without overflow and no device has the room.
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;

    const auto refused = runtime().interop().export_host<std::uint8_t>(count, DeviceIndex{0}, 4096);

    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::AllocationFailed);
}

/** @brief Tests that an export that failed for want of room leaves the runtime able to export again. */
TEST_F(Small_InteropHostExport, AnExportAfterAFailedOneSucceeds)
{
    const std::size_t count = std::numeric_limits<std::size_t>::max() / 2;
    static_cast<void>(runtime().interop().export_host<std::uint8_t>(count, DeviceIndex{0}, 4096));

    const auto exported = runtime().interop().export_host<float>(16, DeviceIndex{0}, 4096);

    EXPECT_TRUE(exported.has_value());
}
