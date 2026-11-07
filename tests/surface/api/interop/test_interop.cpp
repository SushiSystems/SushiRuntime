/**************************************************************************/
/* test_interop.cpp                                                       */
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
 * @file test_interop.cpp
 * @brief Tests importing external memory into a Buffer and borrowing memory as one.
 * @see tests/surface/api/interop/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/buffer_contents.hpp"
#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
#if defined(_WIN32)
    /// @brief The handle kind this platform imports.
    constexpr ExternalHandleKind K_NATIVE_KIND = ExternalHandleKind::OpaqueWin32;
#else
    /// @brief The handle kind this platform imports.
    constexpr ExternalHandleKind K_NATIVE_KIND = ExternalHandleKind::OpaqueFd;
#endif

    /// @brief Size the test descriptors state for the exported allocation.
    constexpr std::uint64_t K_ALLOCATION_BYTES = 64;

    /// @brief Handle value that passes the descriptor checks; no test hands it to a device.
    constexpr std::uint64_t K_PLACEHOLDER_HANDLE = 1;

    /// @brief How many device indices the per-device tests try; a row past the machine's last device skips.
    constexpr std::size_t K_DEVICE_ROWS = 4;

    /** @brief Returns the first device whose device_import equals @p imports, if any. */
    std::optional<std::size_t> first_device_where_import_is(Runtime& runtime, bool imports)
    {
        const std::size_t count = runtime.advanced().device_count();
        for (std::size_t index = 0; index < count; ++index)
            if (runtime.interop().capabilities(DeviceIndex{index}).value().device_import == imports)
                return index;
        return std::nullopt;
    }

    /** @brief Builds a descriptor that device @p index would accept, were its handle real. */
    ExternalMemoryDescriptor acceptable_descriptor(Runtime& runtime, std::size_t index)
    {
        ExternalMemoryDescriptor descriptor{};
        descriptor.kind = K_NATIVE_KIND;
        descriptor.handle = K_PLACEHOLDER_HANDLE;
        descriptor.allocation_bytes = K_ALLOCATION_BYTES;
        descriptor.offset = 0;
        const auto uuid = runtime.interop().capabilities(DeviceIndex{index}).value().device_uuid;
        if (uuid)
            descriptor.device = *uuid;
        return descriptor;
    }

    /** @brief Fails the test unless @p message holds every part of @p parts. */
    void expect_message_holds(const std::string& message, const std::vector<std::string>& parts)
    {
        for (const std::string& part : parts)
            EXPECT_NE(message.find(part), std::string::npos) << part << " is not in: " << message;
    }
} // namespace

/** @brief Fixture of the tests that read one device's capabilities; the parameter is the device index. */
class Medium_InteropCapabilities : public TestSupport::RuntimeTest,
                                   public ::testing::WithParamInterface<std::size_t>
{
    protected:
        /** @brief Skips the row when the machine has no device at the parameter's index. */
        void SetUp() override
        {
            if (GetParam() >= runtime().advanced().device_count())
                GTEST_SKIP() << "the machine has no device " << GetParam();
        }

        /** @brief Returns what the device of this row reports. */
        InteropCapabilities capabilities() const
        {
            return runtime().interop().capabilities(DeviceIndex{GetParam()}).value();
        }
};

/** @brief Tests that host_export equals the host USM aspect of the device behind the queue. */
TEST_P(Medium_InteropCapabilities, HostExportReportsWhetherTheQueueOffersHostUSM)
{
    const sycl::device device = runtime().advanced().queue(DeviceIndex{GetParam()}).get_device();

    EXPECT_EQ(capabilities().host_export, device.has(sycl::aspect::usm_host_allocations));
}

/** @brief Fixture of the tests of a device that imports external memory; a row of another device skips. */
class Medium_InteropImportingDevice : public Medium_InteropCapabilities
{
    protected:
        /** @brief Skips the row when its device imports no external memory. */
        void SetUp() override
        {
            Medium_InteropCapabilities::SetUp();
            if (IsSkipped())
                return;
            if (!capabilities().device_import)
                GTEST_SKIP() << "device " << GetParam() << " imports no external memory";
        }
};

/** @brief Tests that a device that imports external memory reports its UUID. */
TEST_P(Medium_InteropImportingDevice, ReportsItsUUID)
{
    EXPECT_TRUE(capabilities().device_uuid.has_value());
}

INSTANTIATE_TEST_SUITE_P(Devices, Medium_InteropCapabilities,
                         ::testing::Range<std::size_t>(0, K_DEVICE_ROWS));
INSTANTIATE_TEST_SUITE_P(Devices, Medium_InteropImportingDevice,
                         ::testing::Range<std::size_t>(0, K_DEVICE_ROWS));

/** @brief Fixture of the tests that export host memory, which run on a host runtime. */
class Small_InteropExportAsCount : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that an export is host resident and its address is its data. */
TEST_F(Small_InteropExportAsCount, AnExportIsHostResident)
{
    const auto exported =
        runtime().interop().export_host<std::uint32_t>(4, DeviceIndex{0}, 64).value();

    EXPECT_EQ(exported.buffer.residency(), Residency::Host);
    EXPECT_EQ(exported.address(), static_cast<void*>(exported.buffer.data()));
}

/** @brief Tests that sized_from_device takes an export as its count and the task runs that many elements. */
TEST_F(Small_InteropExportAsCount, ALateCountTakesAnExportAsItsCounter)
{
    constexpr std::size_t CAPACITY = 8;
    const auto exported =
        runtime().interop().export_host<std::uint32_t>(4, DeviceIndex{0}, 64).value();
    auto marks = runtime().buffer<int>(CAPACITY).value();
    TestSupport::fill_with(marks, 0);
    exported.buffer[0] = 3u;
    int* out = marks.data();

    auto graph = runtime().graph();
    graph.add_dynamic(sized_from_device(exported.buffer), CAPACITY, Reads(), Writes(marks),
                      [out](std::size_t i) { out[i] = 1; });
    graph.run().value();

    EXPECT_EQ(TestSupport::contents(marks), TestSupport::window_of(CAPACITY, 0, 3, 1, 0));
}

/** @brief Fixture of the import tests that need a device that does not import external memory. */
class Medium_InteropImportOnADeviceWithoutImport : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Finds the device, or skips when every device imports or the toolchain refuses earlier. */
        void SetUp() override
        {
#if !defined(SYCL_EXT_ONEAPI_BINDLESS_IMAGES)
            GTEST_SKIP() << "This toolchain has no bindless images, so import_device refuses before it reads a device.";
#else
            const std::optional<std::size_t> found = first_device_where_import_is(runtime(), false);
            if (!found)
                GTEST_SKIP() << "every device imports external memory";
            index_ = *found;
#endif
        }

        /** @brief The first device that does not import external memory. */
        std::size_t index_ = 0;
};

/** @brief Tests that a device without import refuses with Unsupported and names its index. */
TEST_F(Medium_InteropImportOnADeviceWithoutImport, ADeviceWithoutImportIsRefusedByItsIndex)
{
    const auto imported = runtime().interop().import_device<float>(
        acceptable_descriptor(runtime(), index_), 4, DeviceIndex{index_});

    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code, ErrorCode::Unsupported);
    expect_message_holds(imported.error().message,
                         {"device " + std::to_string(index_), "device_import is false"});
}

/** @brief Fixture of the import tests that need the first device that imports external memory. */
class Medium_InteropImport : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Finds the device, or skips when no device imports. */
        void SetUp() override
        {
            const std::optional<std::size_t> found = first_device_where_import_is(runtime(), true);
            if (!found)
                GTEST_SKIP() << "no device imports external memory";
            index_ = *found;
        }

        /** @brief The first device that imports external memory. */
        std::size_t index_ = 0;
};

/** @brief Tests that importing zero elements returns an empty buffer. */
TEST_F(Medium_InteropImport, ZeroElementsImportNothingAndReturnAnEmptyBuffer)
{
    const Buffer<float> buffer = runtime().interop().import_device<float>(
        acceptable_descriptor(runtime(), index_), 0, DeviceIndex{index_}).value();

    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.size(), 0u);
}

/** @brief Holds one descriptor that import_device refuses: how it differs from an acceptable one and what the refusal says. */
struct RefusedDescriptor
{
    const char* name;                       /**< Names the row. */
    void (*break_it)(ExternalMemoryDescriptor&); /**< Breaks one field of an acceptable descriptor. */
    std::size_t count;                      /**< The element count the import asks for. */
    std::vector<std::string> message_holds; /**< Parts the refusal's message must carry. */
    bool needs_uuid;                        /**< Whether the row needs a device that reports a UUID. */
};

/** @brief Returns the rows of descriptors that import_device refuses by the field at fault. */
std::vector<RefusedDescriptor> refused_descriptors()
{
    std::vector<RefusedDescriptor> rows;
#if defined(_WIN32)
    rows.push_back({"ANullWin32HandleIsRefusedByName",
                    [](ExternalMemoryDescriptor& d) { d.handle = 0; }, 4,
                    {"descriptor.handle is null"}, false});
#else
    rows.push_back({"AFileDescriptorAboveIntIsRefusedWithItsValue",
                    [](ExternalMemoryDescriptor& d)
                    {
                        d.handle = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;
                    },
                    4,
                    {"descriptor.handle is " +
                     std::to_string(static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1)},
                    false});
#endif
    rows.push_back({"ARegionPastTheAllocationIsRefusedWithItsNumbers",
                    [](ExternalMemoryDescriptor& d) { d.offset = 60; }, 4,
                    {"16 bytes at offset 60", "allocation of 64 bytes"}, false});
    rows.push_back({"AnOffsetPastTheAllocationIsRefusedWithoutWrapping",
                    [](ExternalMemoryDescriptor& d) { d.offset = K_ALLOCATION_BYTES + 1; }, 0,
                    {"offset 65"}, false});
    rows.push_back({"AnotherDevicesUUIDIsRefusedWithBothUUIDs",
                    [](ExternalMemoryDescriptor& d)
                    {
                        d.device.bytes[15] = static_cast<std::uint8_t>(d.device.bytes[15] ^ 0xFFu);
                    },
                    4, {"descriptor.device is", "and device"}, true});
    return rows;
}

/** @brief Fixture of the descriptor refusals; the parameter is the row. */
class Medium_InteropImportRefusedDescriptor : public Medium_InteropImport,
                                              public ::testing::WithParamInterface<RefusedDescriptor>
{
    protected:
        /** @brief Skips a row that needs a UUID on a device that reports none. */
        void SetUp() override
        {
            Medium_InteropImport::SetUp();
            if (IsSkipped())
                return;
            if (GetParam().needs_uuid &&
                !runtime().interop().capabilities(DeviceIndex{index_}).value().device_uuid)
                GTEST_SKIP() << "the importing device reports no UUID";
        }
};

/** @brief Tests that import_device refuses the descriptor with InvalidAccess and names the field at fault. */
TEST_P(Medium_InteropImportRefusedDescriptor, IsRefusedWithInvalidAccess)
{
    ExternalMemoryDescriptor descriptor = acceptable_descriptor(runtime(), index_);
    GetParam().break_it(descriptor);

    const auto imported =
        runtime().interop().import_device<float>(descriptor, GetParam().count, DeviceIndex{index_});

    ASSERT_FALSE(imported.has_value());
    EXPECT_EQ(imported.error().code, ErrorCode::InvalidAccess);
    expect_message_holds(imported.error().message, GetParam().message_holds);
}

INSTANTIATE_TEST_SUITE_P(Fields, Medium_InteropImportRefusedDescriptor,
                         ::testing::ValuesIn(refused_descriptors()),
                         [](const ::testing::TestParamInfo<RefusedDescriptor>& info)
                         {
                             return std::string(info.param.name);
                         });

/** @brief Fixture of the borrow tests; the memory they borrow is a Residency::Host buffer of the runtime, which records its kind. */
class Small_InteropBorrow : public TestSupport::HostRuntimeTest
{
    protected:
        /** @brief Allocates @p count zeroed words of Residency::Host memory on device 0 to lend to borrow(). */
        Buffer<std::uint32_t> lent_memory(std::size_t count)
        {
            auto memory = runtime().buffer<std::uint32_t>(count, DeviceIndex{0}, Residency::Host).value();
            TestSupport::fill_with(memory, 0u);
            return memory;
        }
};

/** @brief Tests that a borrowed buffer reports the memory, size, device and residency it was given. */
TEST_F(Small_InteropBorrow, ABorrowedBufferViewsTheCallersMemory)
{
    auto memory = lent_memory(3);
    TestSupport::assign_from(memory, std::vector<std::uint32_t>{3u, 5u, 7u});
    const LivenessToken owner = make_liveness();

    const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
        memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value();

    EXPECT_EQ(borrowed.data(), memory.data());
    EXPECT_EQ(borrowed.size(), 3u);
    EXPECT_EQ(borrowed.device_index(), DeviceIndex{0});
    EXPECT_EQ(borrowed.residency(), Residency::Host);
    EXPECT_EQ(borrowed.host()[2], 7u);
}

/** @brief Tests that a write through a borrowed buffer reaches the caller's memory. */
TEST_F(Small_InteropBorrow, AWriteThroughABorrowedBufferReachesTheCallersMemory)
{
    auto memory = lent_memory(3);
    TestSupport::assign_from(memory, std::vector<std::uint32_t>{3u, 5u, 7u});
    const LivenessToken owner = make_liveness();
    const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
        memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value();

    borrowed[1] = 11u;

    EXPECT_EQ(TestSupport::contents(memory), (std::vector<std::uint32_t>{3u, 11u, 7u}));
}

/** @brief Tests that a borrowed buffer, once destroyed, holds no share of its owner. */
TEST_F(Small_InteropBorrow, ADestroyedBorrowedBufferReleasesItsShareOfTheOwner)
{
    auto memory = lent_memory(3);
    const LivenessToken owner = make_liveness();

    {
        const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
            memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value();
        static_cast<void>(borrowed);
    }

    EXPECT_EQ(owner.use_count(), 1) << "the buffer kept a share of its owner";
}

/** @brief Tests that a run naming a borrowed buffer executes while the owner lives. */
TEST_F(Small_InteropBorrow, ARunNamingABorrowedBufferRunsWhileTheOwnerLives)
{
    auto memory = lent_memory(4);
    std::uint32_t* words = memory.data();
    const LivenessToken owner = make_liveness();
    const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
        memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value();
    auto graph = runtime().graph();
    graph.recorder().host(Access(Reads(), Writes(borrowed)), [words]() { ++words[0]; });

    ASSERT_TRUE(graph.run());

    EXPECT_EQ(memory[0], 1u);
}

/** @brief Tests that a run naming a borrowed buffer whose owner let go is refused and runs nothing. */
TEST_F(Small_InteropBorrow, ARunNamingABorrowedBufferWhoseOwnerLetGoIsRefused)
{
    auto memory = lent_memory(4);
    std::uint32_t* words = memory.data();
    LivenessToken owner = make_liveness();
    const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
        memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value();
    auto graph = runtime().graph();
    graph.recorder().host(Access(Reads(), Writes(borrowed)), [words]() { ++words[0]; });
    owner.reset();

    auto refused = graph.run();

    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::DanglingResource);
    EXPECT_EQ(memory[0], 0u);
}

/** @brief Tests that a run whose borrowed handle was dropped after recording still executes. */
TEST_F(Small_InteropBorrow, DroppingTheBorrowedHandleLeavesTheRunValid)
{
    auto memory = lent_memory(4);
    std::uint32_t* words = memory.data();
    const LivenessToken owner = make_liveness();
    std::optional<Buffer<std::uint32_t>> borrowed(runtime().interop().borrow(
        memory.data(), memory.size(), DeviceIndex{0}, Residency::Host, owner).value());
    auto graph = runtime().graph();
    graph.recorder().host(Access(Reads(), Writes(*borrowed)), [words]() { ++words[0]; });
    borrowed.reset();

    ASSERT_TRUE(graph.run());

    EXPECT_EQ(memory[0], 1u);
}

/** @brief Tests that a runtime buffer borrowed under another residency is refused by its record. */
TEST_F(Small_InteropBorrow, ARuntimeBufferBorrowedAsAnotherResidencyIsRefusedByItsRecord)
{
    const Buffer<std::uint32_t> owned = lent_memory(4);
    const LivenessToken owner = make_liveness();

    const Result<Buffer<std::uint32_t>> refused = runtime().interop().borrow(
        owned.data(), 4, DeviceIndex{0}, Residency::Shared, owner);

    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    expect_message_holds(refused.error().message, {"Residency::Host"});
}

/** @brief Tests that a runtime buffer borrowed under its own residency is accepted. */
TEST_F(Small_InteropBorrow, ARuntimeBufferBorrowedAsItsOwnResidencyIsAccepted)
{
    const Buffer<std::uint32_t> owned = lent_memory(4);
    const LivenessToken owner = make_liveness();

    const Result<Buffer<std::uint32_t>> agreed = runtime().interop().borrow(
        owned.data(), 4, DeviceIndex{0}, Residency::Host, owner);

    EXPECT_TRUE(agreed);
}

/** @brief Fixture of the borrow tests that borrow memory SYCL allocated, which run on the program's runtime. */
class Medium_InteropBorrow : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a range write and read reach shared memory the runtime did not allocate. */
TEST_F(Medium_InteropBorrow, ARangeCopyReachesBorrowedMemoryTheRuntimeDidNotAllocate)
{
    sycl::queue& device_queue = runtime().advanced().queue(DeviceIndex{0});
    std::uint32_t* memory = sycl::malloc_shared<std::uint32_t>(4, device_queue);
    ASSERT_NE(memory, nullptr);
    const LivenessToken owner = make_liveness();
    const std::vector<std::uint32_t> written = {1u, 2u, 3u, 4u};
    std::vector<std::uint32_t> read;

    {
        const Buffer<std::uint32_t> borrowed = runtime().interop().borrow(
            memory, 4, DeviceIndex{0}, Residency::Shared, owner).value();
        ASSERT_TRUE(borrowed.write_range(ElementRange{0, 4},
                                         span<const std::uint32_t>(written.data(), 4)));
        read = borrowed.read_range(ElementRange{1, 2}).value();
    }
    sycl::free(memory, device_queue);

    EXPECT_EQ(read, (std::vector<std::uint32_t>{2u, 3u}));
}

/** @brief Fixture of the borrow tests that allocate host USM, which device 0 must offer. */
class Medium_InteropBorrowHostUSM : public Medium_InteropBorrow
{
    protected:
        /** @brief Skips the test on a machine whose device 0 offers no host USM. */
        void SetUp() override
        {
            if (!runtime().advanced().queue(DeviceIndex{0}).get_device()
                     .has(sycl::aspect::usm_host_allocations))
                GTEST_SKIP() << "device 0 offers no host USM";
        }
};

/** @brief Tests that memory borrowed under a residency other than its own is refused naming both. */
TEST_F(Medium_InteropBorrowHostUSM, AMismatchedResidencyIsRefusedWithBothNames)
{
    sycl::queue& device_queue = runtime().advanced().queue(DeviceIndex{0});
    std::uint32_t* memory = sycl::malloc_host<std::uint32_t>(4, device_queue);
    ASSERT_NE(memory, nullptr);
    const LivenessToken owner = make_liveness();

    const Result<Buffer<std::uint32_t>> refused = runtime().interop().borrow(
        memory, 4, DeviceIndex{0}, Residency::Shared, owner);
    sycl::free(memory, device_queue);

    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    expect_message_holds(refused.error().message, {"Residency::Host", "Residency::Shared"});
}
