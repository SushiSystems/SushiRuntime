/**************************************************************************/
/* test_cross_vendor_copy.cpp                                             */
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
 * @file test_cross_vendor_copy.cpp
 * @brief Tests a buffer written on a CUDA device and read on a HIP device through the host-staged path.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/coherence/transfer_path.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/task/handle_location.hpp>

#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Tests the copy between two devices of two vendors. */
namespace CrossVendorCopyTests
{
    namespace
    {
        /** @brief The size of the buffer the copy moves. */
        constexpr std::size_t K_BYTES = std::size_t{64} * 1024 * 1024;

        /** @brief The number of elements in the buffer. */
        constexpr std::size_t K_COUNT = K_BYTES / sizeof(std::uint32_t);

        /** @brief The multiplier the writing node uses; odd, so the elements of the buffer take different values. */
        constexpr std::uint32_t K_SPREAD = 2654435761u;

        /** @brief Returns whether a backend is the CUDA backend. */
        bool is_cuda(sycl::backend backend) noexcept
        {
#if defined(SYCL_IMPLEMENTATION_ACPP)
            return backend == sycl::backend::cuda;
#else
            return backend == sycl::backend::ext_oneapi_cuda;
#endif
        }

        /** @brief Returns whether a backend is the HIP backend. */
        bool is_hip(sycl::backend backend) noexcept
        {
#if defined(SYCL_IMPLEMENTATION_ACPP)
            return backend == sycl::backend::hip;
#else
            return backend == sycl::backend::ext_oneapi_hip;
#endif
        }

        /** @brief Returns the index of the first GPU of the runtime whose backend @p wanted accepts. */
        template <class Wanted>
        std::optional<std::size_t> first_gpu(const Runtime& runtime, Wanted wanted)
        {
            for (const DeviceDescription& device : runtime.advanced().devices())
                if (device.kind == DeviceKind::GPU && wanted(device.backend))
                    return device.index;
            return std::nullopt;
        }

        /** @brief Returns the name of the first path that connects the device spaces of two devices; empty when none does. */
        std::string first_connecting_path(const Runtime& runtime, std::size_t source, std::size_t destination)
        {
            Internal::Execution::RuntimeContext& context = Internal::ContextAccess::context(runtime);
            const auto& spaces = context.memory_spaces();
            const Internal::Memory::MemorySpace& from = *spaces[Internal::Graph::device_memory_space(source).value()];
            const Internal::Memory::MemorySpace& to = *spaces[Internal::Graph::device_memory_space(destination).value()];
            for (const Internal::Coherence::TransferPath* path : context.transfer_paths())
                if (path->connects(from, to))
                    return std::string(path->name());
            return {};
        }

        /** @brief Returns the seconds a callable takes. */
        template <class Callable>
        double seconds_of(Callable&& callable)
        {
            const auto begin = std::chrono::steady_clock::now();
            callable();
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        }

        /** @brief Returns the host reference of the buffer the reading node writes. */
        std::vector<std::uint32_t> reference_result()
        {
            std::vector<std::uint32_t> expected(K_COUNT);
            for (std::size_t index = 0; index < K_COUNT; ++index)
                expected[index] = static_cast<std::uint32_t>(index) * K_SPREAD + 1u;
            return expected;
        }

        /** @brief Moves K_BYTES from one device to another through a pageable host vector, and returns the seconds it took. */
        double pageable_copy_seconds(const Runtime& runtime, std::size_t source, std::size_t destination,
                                     const std::uint32_t* from, std::uint32_t* to)
        {
            std::vector<std::uint32_t> staging(K_COUNT);
            return seconds_of([&]
            {
                runtime.advanced().queue(DeviceIndex{source}).memcpy(staging.data(), from, K_BYTES).wait();
                runtime.advanced().queue(DeviceIndex{destination}).memcpy(to, staging.data(), K_BYTES).wait();
            });
        }

        /** @brief Prints one bandwidth line: the name, the rate and the time. */
        void print_bandwidth(const std::string& name, double seconds)
        {
            const double mebibytes = static_cast<double>(K_BYTES) / (1024.0 * 1024.0);
            std::cout << name << ' ' << mebibytes / seconds << " MiB/s (" << seconds << " s for "
                      << mebibytes << " MiB)\n";
        }
    } // namespace

    /** @brief Fixture of the cross-vendor tests; they skip on a machine without a CUDA and a HIP GPU. */
    class Large_CrossVendorCopy : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Finds the two GPUs, or skips the test naming the one the machine lacks. */
            void SetUp() override
            {
                const std::optional<std::size_t> cuda = first_gpu(runtime(), is_cuda);
                const std::optional<std::size_t> hip = first_gpu(runtime(), is_hip);
                if (!cuda)
                    GTEST_SKIP() << "the runtime drives no CUDA GPU; the copy needs one to write the buffer.";
                if (!hip)
                    GTEST_SKIP() << "the runtime drives no HIP GPU; the copy needs one to read the buffer.";
                cuda_ = *cuda;
                hip_ = *hip;
            }

            std::size_t cuda_ = 0; /**< Index of the CUDA GPU. */
            std::size_t hip_ = 0;  /**< Index of the HIP GPU. */
    };

    /** @brief Tests that the first path that connects the two devices is the host-staged path, in both directions. */
    TEST_F(Large_CrossVendorCopy, TheDevicesOfTwoVendorsAreConnectedByTheHostStagedPath)
    {
        EXPECT_EQ(first_connecting_path(runtime(), cuda_, hip_), "host_staged");
        EXPECT_EQ(first_connecting_path(runtime(), hip_, cuda_), "host_staged");
    }

    /**
     * @brief Tests that a 64 MiB buffer written by a node on the CUDA GPU reads equal on the HIP GPU.
     *
     * Each node sits where the buffer it writes lives. The host-staged path is the only path between
     * the two devices, so the copy the plan adds takes it. The test prints the bandwidth of the whole
     * run and of a copy through pageable host memory; it asserts neither.
     */
    TEST_F(Large_CrossVendorCopy, ABufferWrittenOnTheCUDADeviceReadsEqualOnTheHIPDevice)
    {
        const Runtime& rt = runtime();
        auto written = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{cuda_}).value();
        auto result = rt.buffer<std::uint32_t>(K_COUNT, DeviceIndex{hip_}).value();
        auto world = rt.dynamic_graph();
        Recorder& recorder = world.region(0).recorder();
        recorder.captured_kernel(K_COUNT, Access(Reads(), Writes(written)), std::make_tuple(written.data()),
                                 std::vector<GlobalHandle>{written.handle()},
                                 [](std::size_t index, std::uint32_t* target)
                                 { target[index] = static_cast<std::uint32_t>(index) * K_SPREAD; });
        recorder.captured_kernel(K_COUNT, Access(Reads(written), Writes(result)),
                                 std::make_tuple(static_cast<const std::uint32_t*>(written.data()), result.data()),
                                 std::vector<GlobalHandle>{written.handle(), result.handle()},
                                 [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                                 { target[index] = source[index] + 1u; });

        const double run_seconds = seconds_of([&] { EXPECT_TRUE(world.run()); });

        EXPECT_EQ(TestSupport::contents(result), reference_result());
        print_bandwidth("cross_vendor.write_copy_read.pinned_ring", run_seconds);
        print_bandwidth("cross_vendor.copy_only.pageable",
                        pageable_copy_seconds(rt, cuda_, hip_, written.data(), result.data()));
    }
} // namespace CrossVendorCopyTests
