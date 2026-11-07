/**************************************************************************/
/* test_runtime_context.cpp                                               */
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
 * @file test_runtime_context.cpp
 * @brief Tests the RuntimeContext: its profiling queues, its topology and its shutdown order.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <string>
#include <tuple>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/report/recording_observer.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/usm_guard/usm_shared.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Builds a host context on a seeded executor and a manual clock. */
    std::shared_ptr<Internal::Execution::RuntimeContext> make_host_context()
    {
        Internal::Execution::HostContextParts parts;
        parts.executor = std::make_shared<SeededExecutor>(TestSupport::K_HOST_RUNTIME_SEED);
        parts.clock = std::make_shared<ManualClock>();
        return Internal::Execution::RuntimeContext::create_host(parts).value();
    }
} // namespace

/** @brief Tests the profiling queue RuntimeContext::devices() builds against the plain queue. */
namespace ProfilingQueueTests
{
    using namespace SushiRuntime::Internal::Execution;
    using SushiRuntime::Test::USMShared;

    namespace
    {
        /** @brief Names the out-of-order profiling queue of a device. */
        constexpr Internal::Device::QueueFlavour K_PROFILING_FLAVOUR{false, true};

        /** @brief Lists, per device, whether its profiling queue has been built. */
        std::vector<bool> profiling_queue_flags(const Internal::Device::DeviceQueueSet& devices)
        {
            std::vector<bool> flags;
            for (std::size_t i = 0; i < devices.count(); ++i)
                flags.push_back(devices.has_profiling_queue(i));
            return flags;
        }

        /** @brief Returns the flags of @p count devices of which only device 0 is built. */
        std::vector<bool> only_device_zero(std::size_t count)
        {
            std::vector<bool> flags(count, false);
            flags[0] = true;
            return flags;
        }

        /** @brief One action on a fresh context and whether it builds the profiling queue of device 0. */
        struct LazyBuildRow
        {
            const char* action;                                      /**< Names the action. */
            void (*act)(Internal::Device::DeviceQueueSet& devices);  /**< Runs the action. */
            bool builds_device_zero;                                 /**< Whether the action builds device 0's queue. */
        };

        /** @brief Does nothing to the context. */
        void ask_nothing(Internal::Device::DeviceQueueSet&)
        {
        }

        /** @brief Asks for the plain queue of device 0. */
        void ask_for_the_plain_queue(Internal::Device::DeviceQueueSet& devices)
        {
            static_cast<void>(devices.queue(0));
        }

        /** @brief Asks for the profiling queue of device 0. */
        void ask_for_the_profiling_queue(Internal::Device::DeviceQueueSet& devices)
        {
            static_cast<void>(devices.queue(0, K_PROFILING_FLAVOUR));
        }
    } // namespace

    /**
     * @brief Tests that the profiling queue carries enable_profiling and shares the plain queue's
     *        context.
     */
    TEST(Medium_ProfilingQueue, ProfilingQueueAddsThePropertyAndSharesTheContext)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;

        sycl::queue& plain = context.devices().queue(0);
        EXPECT_FALSE(plain.has_property<sycl::property::queue::enable_profiling>())
            << "the plain queue must be unchanged from today's non-profiling shape";

        sycl::queue& profiling = context.devices().queue(0, K_PROFILING_FLAVOUR);
        EXPECT_TRUE(profiling.has_property<sycl::property::queue::enable_profiling>());
        EXPECT_EQ(profiling.get_context(), plain.get_context())
            << "the two queues must share one context so USM stays legal on either";
    }

    /** @brief Tests that a profiling queue is built only when asked for, and only on the device asked. */
    TEST(Medium_ProfilingQueue, AProfilingQueueIsBuiltOnlyWhenItIsAskedFor)
    {
        const LazyBuildRow rows[] = {
            {"no request", &ask_nothing, false},
            {"the plain queue", &ask_for_the_plain_queue, false},
            {"the profiling queue of device 0", &ask_for_the_profiling_queue, true},
        };

        for (const LazyBuildRow& row : rows)
        {
            const auto context_owner = SushiRuntime::Test::make_context();
            Internal::Device::DeviceQueueSet& devices = (*context_owner).devices();
            const std::size_t count = devices.count();

            row.act(devices);

            EXPECT_EQ(profiling_queue_flags(devices),
                      row.builds_device_zero ? only_device_zero(count) : std::vector<bool>(count, false))
                << row.action;
        }
    }

    /** @brief Tests that the profiling flavour is a queue of its own, apart from the plain one. */
    TEST(Medium_ProfilingQueue, TheProfilingFlavourIsNotThePlainQueue)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;

        EXPECT_FALSE(context.devices().queue(0) == context.devices().queue(0, K_PROFILING_FLAVOUR))
            << "a plain queue records no timestamps, so the profiling flavour cannot alias it";
    }

    /**
     * @brief Tests that a kernel on the profiling queue dereferences USM allocated through the plain
     *        queue.
     */
    TEST(Medium_ProfilingQueue, USMFromThePlainQueueIsUsableOnTheProfilingQueue)
    {
        constexpr std::size_t N = 256;

        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        sycl::queue& plain = context.devices().queue(0);
        sycl::queue& profiling = context.devices().queue(0, K_PROFILING_FLAVOUR);

        USMShared<int> usm(plain, N);
        ASSERT_TRUE(usm);
        int* data = usm.get();
        std::iota(data, data + N, 0);
        std::vector<int> doubled(N);
        std::iota(doubled.begin(), doubled.end(), 0);
        std::transform(doubled.begin(), doubled.end(), doubled.begin(), [](int value) { return value * 2; });

        profiling.submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
            {
                data[idx] = data[idx] * 2;
            });
        }).wait_and_throw();

        EXPECT_EQ(std::vector<int>(data, data + N), doubled)
            << "the profiling queue's kernel did not double every element";
    }
} // namespace ProfilingQueueTests

/** @brief Tests hardware discovery, NUMA distances and the invalid-NUMA fallbacks. */
namespace TopologyTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Topology;

    namespace
    {
        /** @brief Holds the number of USM kinds the aligned allocation tests cover. */
        constexpr std::size_t K_ALIGNED_KIND_COUNT = 3;

        /** @brief Lists the USM kinds the aligned allocation tests cover: shared, device, host. */
        const std::array<Internal::Memory::USMKind, K_ALIGNED_KIND_COUNT> K_ALIGNED_KINDS = {
            Internal::Memory::USMKind::Shared,
            Internal::Memory::USMKind::Device,
            Internal::Memory::USMKind::Host,
        };

        /**
         * @brief Returns whether the toolchain's aligned shared allocation honours @p alignment.
         *
         * Several allocations are live at once, so a heap that happens to hand out one aligned
         * address does not pass the probe.
         */
        bool shared_allocation_honours(sycl::queue& queue, std::size_t alignment)
        {
            constexpr std::size_t K_PROBES = 8;
            std::array<void*, K_PROBES> probes{};
            bool honoured = true;
            for (void*& probe : probes)
            {
                probe = sycl::aligned_alloc_shared(alignment, 1, queue);
                honoured = honoured && probe != nullptr
                           && reinterpret_cast<std::uintptr_t>(probe) % alignment == 0u;
            }
            for (void* const probe : probes)
                sycl::free(probe, queue);
            return honoured;
        }

        /** @brief Holds a context for the tests of the allocator that run on it. */
        class ContextFixture : public ::testing::Test
        {
            protected:
                /** @brief Builds the context. */
                void SetUp() override
                {
                    context_owner_ = SushiRuntime::Test::make_context();
                }

                /** @brief Returns the context. */
                RuntimeContext& context() { return *context_owner_; }

            private:
                std::shared_ptr<RuntimeContext> context_owner_; /**< The context of the test. */
        };

        /** @brief Holds a host context for the tests of the allocator that need no device. */
        class HostContextFixture : public ::testing::Test
        {
            protected:
                /** @brief Builds the host context. */
                void SetUp() override
                {
                    context_owner_ = make_host_context();
                }

                /** @brief Returns the context. */
                RuntimeContext& context() { return *context_owner_; }

            private:
                std::shared_ptr<RuntimeContext> context_owner_; /**< The context of the test. */
        };

        /** @brief Names a USM kind and an alignment in the byte count, for the parameter tables. */
        using AlignedRequest = std::tuple<Internal::Memory::USMKind, std::size_t, std::size_t>;
    } // namespace

    /** @brief Fixture of the tests that run a queue for an id no node has. */
    class Medium_InvalidNUMAQueue : public ContextFixture, public ::testing::WithParamInterface<int>
    {
    };

    /** @brief Tests that a queue for an id no node has runs a host task. */
    TEST_P(Medium_InvalidNUMAQueue, RunsAHostTask)
    {
        std::atomic<bool> ran{false};

        sycl::queue& fallback_queue = context().devices().queue_or_numa_local(std::nullopt, GetParam());
        fallback_queue.submit([&ran](sycl::handler& h)
        {
            SushiRuntime::Detail::enqueue_host_task(h, [&ran]() { ran.store(true); });
        }).wait();

        EXPECT_TRUE(ran.load()) << "the fallback queue did not run the host task";
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_InvalidNUMAQueue, ::testing::Values(999, -5));

    /** @brief Fixture of the tests that allocate on an id no node has. */
    class Small_InvalidNUMAAllocation : public HostContextFixture
    {
    };

    /**
     * @brief Tests that an allocation on an out-of-range NUMA ID falls back to a usable node.
     *
     * A negative id is what a caller gets from a topology that reported no NUMA
     * information, so it has to resolve to something rather than index out of
     * bounds. The pointer's owner is recorded against the fallback device.
     */
    TEST_F(Small_InvalidNUMAAllocation, AnAllocationOnAnInvalidNUMAFallsBackAndIsRecorded)
    {
        constexpr int INVALID_NUMA_ID = -5;

        void* const ptr = context().memory().allocate({Internal::Memory::USMKind::Shared, 128, 0,
                                                       context().devices().device_for_numa(INVALID_NUMA_ID)});

        ASSERT_NE(ptr, nullptr) << "Fallback allocation returned null.";
        EXPECT_TRUE(context().memory().device_of(ptr).has_value())
            << "The fallback allocation was not recorded against any device.";
        context().memory().release(ptr);
    }

    /** @brief Tests that releasing a fallback allocation clears the record of its owner. */
    TEST_F(Small_InvalidNUMAAllocation, ReleasingAFallbackAllocationClearsItsOwner)
    {
        constexpr int INVALID_NUMA_ID = -5;
        void* const ptr = context().memory().allocate({Internal::Memory::USMKind::Shared, 128, 0,
                                                       context().devices().device_for_numa(INVALID_NUMA_ID)});
        ASSERT_NE(ptr, nullptr);

        context().memory().release(ptr);

        EXPECT_FALSE(context().memory().device_of(ptr).has_value())
            << "Freeing left the pointer's owner recorded.";
    }

    /** @brief Fixture of the tests that read the live USM count. */
    class Small_LiveUSMCount : public HostContextFixture
    {
    };

    /** @brief Tests that the live count follows each allocation and release. */
    TEST_F(Small_LiveUSMCount, FollowsAllocateAndRelease)
    {
        const std::size_t baseline = context().memory().live_count();

        void* shared = context().memory().allocate(
            {Internal::Memory::USMKind::Shared, 128, 0, context().devices().device_for_numa(0)});
        void* device = context().memory().allocate(
            {Internal::Memory::USMKind::Device, 128, 0, context().devices().device_for_numa(0)});
        ASSERT_NE(shared, nullptr);
        ASSERT_NE(device, nullptr);
        EXPECT_EQ(context().memory().live_count(), baseline + 2);

        context().memory().release(shared);
        EXPECT_EQ(context().memory().live_count(), baseline + 1);

        context().memory().release(device);
        EXPECT_EQ(context().memory().live_count(), baseline);
    }

    /** @brief Tests that the live count follows an adopted record and its forgetting. */
    TEST_F(Small_LiveUSMCount, FollowsAdoptAndForget)
    {
        const std::size_t baseline = context().memory().live_count();
        int external = 0;

        context().memory().adopt(&external, 0, sizeof(external), std::nullopt);
        EXPECT_EQ(context().memory().live_count(), baseline + 1);

        context().memory().forget(&external);
        EXPECT_EQ(context().memory().live_count(), baseline);
    }

    /** @brief Fixture of the aligned allocation rows: a kind, an alignment and a byte count. */
    class Medium_AlignedAllocation : public ContextFixture, public ::testing::WithParamInterface<AlignedRequest>
    {
        protected:
            /** @brief Skips a shared request on a toolchain that does not honour its alignment. */
            void SetUp() override
            {
                ContextFixture::SetUp();
                if (std::get<0>(GetParam()) == Internal::Memory::USMKind::Shared
                    && !shared_allocation_honours(context().devices().queue(0), std::get<1>(GetParam())))
                    GTEST_SKIP() << "the toolchain's aligned shared allocation ignores alignment "
                                 << std::get<1>(GetParam());
            }
    };

    /** @brief Tests that the address starts on the requested alignment. */
    TEST_P(Medium_AlignedAllocation, StartsOnTheAlignment)
    {
        const auto [kind, alignment, bytes] = GetParam();

        void* const ptr = context().memory().allocate({kind, bytes, alignment, 0});

        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0u);
        context().memory().release(ptr);
    }

    INSTANTIATE_TEST_SUITE_P(
        Rows, Medium_AlignedAllocation,
        ::testing::Combine(::testing::ValuesIn(K_ALIGNED_KINDS), ::testing::Values(std::size_t{64}, std::size_t{128}),
                           ::testing::Values(std::size_t{1}, std::size_t{100}, std::size_t{4096})));

    /** @brief Fixture of the rows that allocate one aligned block of a kind. */
    class Small_AlignedRecording : public HostContextFixture,
                                   public ::testing::WithParamInterface<Internal::Memory::USMKind>
    {
    };

    /** @brief Tests that the block is recorded once and dropped by release(). */
    TEST_P(Small_AlignedRecording, IsRecordedOnceAndDroppedByRelease)
    {
        const std::size_t baseline = context().memory().live_count();

        void* const ptr = context().memory().allocate({GetParam(), 256, 128, 0});

        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(context().memory().live_count(), baseline + 1);
        context().memory().release(ptr);
        EXPECT_EQ(context().memory().live_count(), baseline);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_AlignedRecording, ::testing::ValuesIn(K_ALIGNED_KINDS));

    /** @brief Tests that an aligned allocation on a device index past the last one stops. */
    TEST(Medium_RuntimeContextDeathTest, AnAlignedAllocationOnAMissingDeviceStops)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        const std::size_t missing_device = ctx.devices().count();

        EXPECT_DEATH(static_cast<void>(ctx.memory().allocate(
                         {Internal::Memory::USMKind::Shared, 64, 128, missing_device})),
                     "USM device index");
        EXPECT_DEATH(static_cast<void>(ctx.memory().allocate(
                         {Internal::Memory::USMKind::Device, 64, 128, missing_device})),
                     "USM device index");
    }
} // namespace TopologyTests

/** @brief Tests that an NVIDIA GPU is discovered through the CUDA backend and mapped to a NUMA node. */
namespace NvidiaTopologyTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Topology;
    using SushiRuntime::Test::USMShared;

    /**
     * @brief Finds any NVIDIA GPU among discovered hardware nodes.
     *
     * Scans the provided list of nodes for a GPU matching NVIDIA-specific keywords like "GeForce", "GTX", or "RTX".
     *
     * @param nodes The vector of discovered hardware nodes.
     * @return A pointer to the matching hardware node, or nullptr if none is found.
     */
    static const HardwareNode* find_nvidia_node(const std::vector<HardwareNode>& nodes)
    {
        for (const auto& n : nodes)
        {
            if (n.is_gpu && n.name.find("NVIDIA") != std::string::npos)
                return &n;
            if (n.is_gpu && (n.name.find("GeForce") != std::string::npos ||
                             n.name.find("GTX")     != std::string::npos ||
                             n.name.find("RTX")     != std::string::npos))
                return &n;
        }
        return nullptr;
    }

    /**
     * @brief Checks if any GPU device is available on a CUDA platform.
     *
     * Queries the SYCL backend for GPU devices and verifies if their platform name contains "CUDA".
     *
     * @return True if a CUDA-backed GPU is available, false otherwise.
     */
    static bool cuda_device_available()
    {
        for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::gpu))
        {
            std::string pname = dev.get_platform().get_info<sycl::info::platform::name>();
            if (pname.find("CUDA") != std::string::npos ||
                pname.find("cuda") != std::string::npos)
                return true;
        }
        return false;
    }

    /**
     * @brief Returns the queue of the first CUDA device of @p context, in the order of the SYCL device list.
     *
     * @return The queue, or nullptr when the context holds no CUDA device.
     */
    static sycl::queue* cuda_queue(RuntimeContext& context)
    {
        const auto devices = sycl::device::get_devices(sycl::info::device_type::all);
        for (std::size_t i = 0; i < devices.size(); ++i)
        {
            const std::string platform = devices[i].get_platform().get_info<sycl::info::platform::name>();
            if (platform.find("CUDA") != std::string::npos || platform.find("cuda") != std::string::npos)
                return &context.devices().queue(i);
        }
        return nullptr;
    }

    /** @brief Tells whether the process runs under WSL, which declares itself in WSL_DISTRO_NAME; never true on Windows. */
    static bool running_under_wsl()
    {
#if defined(_WIN32)
        return false;
#else
        return std::getenv("WSL_DISTRO_NAME") != nullptr;
#endif
    }

    /** @brief Fixture of the tests that need a CUDA device; they skip where the machine has none. */
    class Large_RuntimeContextNvidia : public ::testing::Test
    {
        protected:
            /** @brief Skips the test on a machine without a CUDA-capable GPU. */
            void SetUp() override
            {
                if (!cuda_device_available())
                    GTEST_SKIP() << "No CUDA-capable GPU found.";
            }
    };

    /**
     * @brief Verifies that an NVIDIA GPU is discovered when CUDA is active.
     *
     * Probes all devices using HardwareDiscovery and asserts that an NVIDIA GPU is found.
     */
    TEST_F(Large_RuntimeContextNvidia, NvidiaGPUDiscovered)
    {
        HardwareDiscovery discovery;
        DeviceFilter config;
        config.include_cpu = true;
        config.include_gpu = true;

        auto nodes = discovery.probe_all_devices(config).value();
        const HardwareNode* nvidia = find_nvidia_node(nodes);

        ASSERT_NE(nvidia, nullptr) << "NVIDIA GPU not found in discovered hardware nodes.";
        EXPECT_TRUE(nvidia->is_gpu);
        EXPECT_FALSE(nvidia->name.empty());
    }

    /**
     * @brief Verifies that a discovered NVIDIA GPU is assigned a valid NUMA node ID.
     *
     * Maps NUMA distances for the discovered nodes and asserts that the NVIDIA GPU has a valid ID.
     * Under WSL, which gives hwloc no PCI devices, the mapping cannot resolve and the test skips.
     */
    TEST_F(Large_RuntimeContextNvidia, NvidiaGPUHasNUMAMapping)
    {
        if (running_under_wsl())
            GTEST_SKIP() << "WSL gives hwloc no PCI devices, so the GPU maps to the fallback node.";

        HardwareDiscovery discovery;
        DeviceFilter config;
        config.include_gpu = true;
        auto nodes = discovery.probe_all_devices(config).value();
        ASSERT_NE(find_nvidia_node(nodes), nullptr);

        NUMAMapper mapper;
        mapper.map_distances(nodes); // mutates nodes in-place, filling numa_node_id

        const HardwareNode* mapped_nvidia = find_nvidia_node(nodes);
        ASSERT_NE(mapped_nvidia, nullptr);
        EXPECT_GE(mapped_nvidia->numa_node_id, 0) << "The NVIDIA GPU must be assigned to a valid NUMA node.";
    }

    /**
     * @brief Verifies that the RuntimeContext exposes a functional CUDA-backed SYCL queue.
     *
     * Runs a trivial host_task on the CUDA queue and asserts that it ran.
     */
    TEST_F(Large_RuntimeContextNvidia, RuntimeContextExposesNvidiaQueue)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        sycl::queue* const queue = cuda_queue(*ctx_owner);
        ASSERT_NE(queue, nullptr) << "RuntimeContext found no CUDA-backed queue.";
        std::atomic<bool> ran{false};

        queue->submit([&ran](sycl::handler& h)
        {
            SushiRuntime::Detail::enqueue_host_task(h, [&ran] { ran.store(true); });
        }).wait();

        EXPECT_TRUE(ran.load());
    }

    /**
     * @brief Performs an end-to-end vector addition smoke test on an NVIDIA GPU.
     *
     * Allocates USM shared memory, submits a parallel_for kernel on the CUDA queue, and verifies the addition results.
     */
    TEST_F(Large_RuntimeContextNvidia, NvidiaGPUVectorAdd)
    {
        constexpr std::size_t N = 4096;
        const auto ctx_owner = SushiRuntime::Test::make_context();
        sycl::queue* const cuda_q = cuda_queue(*ctx_owner);
        ASSERT_NE(cuda_q, nullptr) << "No CUDA-backed queue in RuntimeContext.";
        USMShared<float> usm_A(*cuda_q, N);
        USMShared<float> usm_B(*cuda_q, N);
        USMShared<float> usm_C(*cuda_q, N);
        ASSERT_TRUE(usm_A);
        ASSERT_TRUE(usm_B);
        ASSERT_TRUE(usm_C);
        float* A = usm_A.get();
        float* B = usm_B.get();
        float* C = usm_C.get();
        std::iota(A, A + N, 0.f);
        std::transform(A, A + N, B, [](float value) { return value * 2.f; });
        std::fill(C, C + N, 0.f);
        std::vector<float> expected(N);
        std::transform(A, A + N, B, expected.begin(), std::plus<float>());

        cuda_q->submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
            {
                C[idx] = A[idx] + B[idx];
            });
        }).wait();

        EXPECT_EQ(std::vector<float>(C, C + N), expected) << "The vector addition produced wrong results.";
    }
} // namespace NvidiaTopologyTests

/** @brief Tests the injectable topology provider and NUMA distance model on single-node hardware. */
namespace TopologyAbstractionTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Topology;

    /**
     * @brief Verifies that RuntimeContext correctly utilizes an injected distance model over concrete
     *        hwloc.
     *
     * Injects a synthetic distance model into the RuntimeContext and ensures its reported NUMA distances match the injected values.
     */
    TEST(Medium_RuntimeContextTopology, InjectedDistanceModelDrivesRuntimeContext)
    {
        auto provider = std::make_shared<HwlocTopologyProvider>();
        auto distance = std::make_shared<MatrixDistanceModel>(
            std::vector<std::vector<int>>{{10, 21},
                                          {21, 10}});

        const auto ctx_owner = SushiRuntime::Test::make_context({}, provider, distance);
        RuntimeContext& ctx = *ctx_owner;

        EXPECT_EQ(ctx.topology().numa_distance(0, 0), 10);
        EXPECT_EQ(ctx.topology().numa_distance(0, 1), 21);
    }

    /** @brief Fixture of the rows that route a queue through one synthetic NUMA node. */
    class Medium_RuntimeContextSyntheticNUMA : public ::testing::TestWithParam<int>
    {
    };

    TEST_P(Medium_RuntimeContextSyntheticNUMA, RoutesAQueueThatRunsAHostTask)
    {
        constexpr int FAKE_NODES = 2;

        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), FAKE_NODES);
        auto distance = std::make_shared<MatrixDistanceModel>(
            std::vector<std::vector<int>>{{10, 21},
                                          {21, 10}});
        const auto ctx_owner = SushiRuntime::Test::make_context({}, provider, distance);
        std::atomic<bool> ran{false};

        sycl::queue& q = (*ctx_owner).devices().queue_or_numa_local(std::nullopt, GetParam());
        q.submit([&ran](sycl::handler& h)
        {
            SushiRuntime::Detail::enqueue_host_task(h, [&ran] { ran.store(true); });
        }).wait();

        EXPECT_TRUE(ran.load()) << "Synthetic NUMA node " << GetParam() << " produced no working queue.";
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Medium_RuntimeContextSyntheticNUMA, ::testing::Values(0, 1));

    /**
     * @brief Verifies that a GPU node carrying no PCI address is placed on the fallback NUMA node.
     *
     * Builds a node by hand, leaves its PCI address empty and asserts NUMAMapper puts it on node 0.
     */
    TEST(Small_RuntimeContextTopology, GPUWithoutPCIAddressTakesFallbackNUMANode)
    {
        HardwareNode gpu;
        gpu.name = "synthetic GPU without a PCI address";
        gpu.is_gpu = true;

        ASSERT_FALSE(gpu.pci_address.has_value());
        ASSERT_EQ(gpu.numa_node_id, -1);

        std::vector<HardwareNode> nodes{gpu};
        NUMAMapper mapper;
        mapper.map_distances(nodes);

        EXPECT_FALSE(nodes[0].pci_address.has_value());
        EXPECT_EQ(nodes[0].numa_node_id, 0);
    }
} // namespace TopologyAbstractionTests

/** @brief Tests that RuntimeContext::shutdown() stops the offload coordinator before the pool. */
namespace OffloadSeamShutdownTests
{
    namespace
    {
        using SushiRuntime::make_sushi;
        using SushiRuntime::sushi_ptr;
        using SushiRuntime::Internal::Graph::Node;

        /** @brief Records, in its first stop(), whether the worker pool still runs a node. */
        class PoolProbingCoordinator final : public SushiRuntime::Internal::Graph::OffloadCoordinator
        {
            public:
                void bind(SushiRuntime::Internal::Graph::OffloadSink& sink) override
                {
                    sink_ = &sink;
                }

                void prepare(SushiRuntime::span<const sushi_ptr<Node>>,
                             const std::shared_ptr<const SushiRuntime::PlacementRules>&) override
                {

                }

                [[nodiscard]] bool try_offload(Node&) override
                {
                    return false;
                }

                void on_local_completion(Node&) noexcept override
                {

                }

                /** @brief Hands the pool a probe node on the first call and waits for it to run. */
                void stop() noexcept override
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (++stop_calls_ > 1 || sink_ == nullptr)
                        return;

                    probe_ = make_sushi<Node>();
                    probe_->mode = Node::ExecutionMode::Host;
                    probe_->submits_device_work_ = false;
                    probe_->native_work = [this] { mark_probe_ran(); };

                    // resubmit() consumes one reference, as it does for a node try_offload() accepted.
                    probe_->add_ref();
                    lock.unlock();
                    sink_->resubmit(*probe_);
                    lock.lock();
                    pool_ran_probe_ = probe_ran_cv_.wait_for(lock, K_PROBE_BUDGET,
                                                             [this] { return probe_ran_; });
                }

                /** @brief Returns how many times stop() was called. */
                [[nodiscard]] int stop_calls() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return stop_calls_;
                }

                /** @brief Returns whether the pool ran the probe before the first stop() returned. */
                [[nodiscard]] bool pool_ran_probe() const
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return pool_ran_probe_;
                }

            private:
                /** @brief Runs on a pool worker as the probe node's work. */
                void mark_probe_ran()
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        probe_ran_ = true;
                    }
                    probe_ran_cv_.notify_all();
                }

                /// @brief Longest wait for the probe; a stopped pool never runs it.
                static constexpr std::chrono::seconds K_PROBE_BUDGET{10};

                /// @brief Guards every member below.
                mutable std::mutex mutex_;
                /// @brief Signalled when the probe node has run.
                std::condition_variable probe_ran_cv_;
                /// @brief The scheduler, as bind() received it.
                SushiRuntime::Internal::Graph::OffloadSink* sink_ = nullptr;
                /// @brief The node the first stop() hands to the pool.
                sushi_ptr<Node> probe_;
                /// @brief Calls of stop().
                int stop_calls_ = 0;
                /// @brief Set by the probe node on a pool worker.
                bool probe_ran_ = false;
                /// @brief Whether the probe ran within the budget of the first stop().
                bool pool_ran_probe_ = false;
        };
    } // namespace

    /**
     * @brief shutdown() stops the installed coordinator while the worker pool still runs.
     */
    TEST(Medium_RuntimeContextOffloadSeam, ShutdownStopsCoordinatorBeforeEngine)
    {
        const std::shared_ptr<PoolProbingCoordinator> coordinator =
            std::make_shared<PoolProbingCoordinator>();

        {
            const auto context_owner = SushiRuntime::Test::make_context();
            SushiRuntime::Internal::Execution::RuntimeContext& context = *context_owner;
            context.install_offload(coordinator);

            context.shutdown();

            EXPECT_EQ(coordinator->stop_calls(), 1) << "shutdown() did not stop the coordinator";
            EXPECT_TRUE(coordinator->pool_ran_probe())
                << "the worker pool had stopped before the coordinator's stop() returned";
        }

        // The destructor stops the coordinator again, which the seam allows; see README.md.
        EXPECT_EQ(coordinator->stop_calls(), 2);
    }
} // namespace OffloadSeamShutdownTests

namespace RunObserverWiringTests
{
    /** @brief Fixture of the observer wiring test: a host context. */
    class Small_RuntimeContextObservers : public ::testing::Test
    {
        protected:
            /** @brief Builds the host context. */
            void SetUp() override
            {
                context_owner_ = make_host_context();
            }

            /** @brief Returns the context. */
            SushiRuntime::Internal::Execution::RuntimeContext& context() { return *context_owner_; }

        private:
            std::shared_ptr<SushiRuntime::Internal::Execution::RuntimeContext> context_owner_; /**< The test's context. */
    };

    /**
     * @brief Tests that an observer added to the context hears the runs the context delivers until it is removed.
     *
     * The gate rules are tested on RunObserverRegistry, which the context owns.
     */
    TEST_F(Small_RuntimeContextObservers, AnAddedObserverHearsRunsUntilItIsRemoved)
    {
        const auto observer = std::make_shared<SushiRuntime::RecordingObserver>();
        const std::uint64_t id = context().add_run_observer(observer);
        const SushiRuntime::RunObservation observation;
        context().notify_run(observation);
        EXPECT_TRUE(context().wants_device_timing());

        context().remove_run_observer(id);
        context().notify_run(observation);

        EXPECT_EQ(observer->count(), 1u);
        EXPECT_FALSE(context().wants_device_timing());
    }
} // namespace RunObserverWiringTests
