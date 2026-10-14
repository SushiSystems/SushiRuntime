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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_runtime_context.cpp
 * @brief Tests the RuntimeContext: its profiling queues, its topology and its shutdown order.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <chrono>
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

#include <SushiRuntime/device/queue_flavour.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
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

using namespace SushiRuntime;

/** @brief Tests the profiling queue RuntimeContext::devices() builds against the plain queue. */
namespace ProfilingQueueTests
{
    using namespace SushiRuntime::Internal::Execution;
    using SushiRuntime::Test::USMShared;

    namespace
    {
        /** @brief Names the out-of-order profiling queue of a device. */
        constexpr Internal::Device::QueueFlavour K_PROFILING_FLAVOUR{false, true};
    } // namespace

    /**
     * @brief Tests that the profiling queue carries enable_profiling and shares the plain queue's
     *        context.
     */
    TEST(Integration_ProfilingQueue, ProfilingQueueAddsThePropertyAndSharesTheContext)
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

    /** @brief Tests that no profiling queue exists before the first request. */
    TEST(Integration_ProfilingQueue, NoProfilingQueueExistsBeforeTheFirstRequest)
    {
        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;

        for (std::size_t i = 0; i < context.devices().count(); ++i)
            EXPECT_FALSE(context.devices().has_profiling_queue(i))
                << "device " << i << " must not have a profiling queue yet";

        (void)context.devices().queue(0, K_PROFILING_FLAVOUR);

        EXPECT_TRUE(context.devices().has_profiling_queue(0));
        for (std::size_t i = 1; i < context.devices().count(); ++i)
            EXPECT_FALSE(context.devices().has_profiling_queue(i))
                << "requesting device 0 must not build device " << i << "'s queue";
    }

    /** @brief Tests that RuntimeSettings::profiling builds every device's profiling queue eagerly. */
    TEST(Integration_ProfilingQueue, SettingsProfilingBuildsEveryDeviceEagerly)
    {
        RuntimeSettings settings;
        settings.profiling = true;
        const auto context_owner = SushiRuntime::Test::make_context({}, nullptr, nullptr, settings);
        RuntimeContext& context = *context_owner;

        for (std::size_t i = 0; i < context.devices().count(); ++i)
            EXPECT_TRUE(context.devices().has_profiling_queue(i))
                << "device " << i << "'s profiling queue must exist before any request";
    }

    /**
     * @brief Tests that the profiling flavour returns the plain queue itself when
     *        RuntimeSettings::profiling is on.
     */
    TEST(Integration_ProfilingQueue, SettingsProfilingReturnsThePlainQueueItself)
    {
        RuntimeSettings settings;
        settings.profiling = true;
        const auto context_owner = SushiRuntime::Test::make_context({}, nullptr, nullptr, settings);
        RuntimeContext& context = *context_owner;

        EXPECT_TRUE(context.devices().queue(0) == context.devices().queue(0, K_PROFILING_FLAVOUR))
            << "the profiling flavour must alias the already-profiling plain queue, not duplicate it";
    }

    /**
     * @brief Tests that a kernel on the profiling queue dereferences USM allocated through the plain
     *        queue.
     */
    TEST(Integration_ProfilingQueue, USMFromThePlainQueueIsUsableOnTheProfilingQueue)
    {
        constexpr std::size_t N = 256;

        const auto context_owner = SushiRuntime::Test::make_context();
        RuntimeContext& context = *context_owner;
        sycl::queue& plain = context.devices().queue(0);
        sycl::queue& profiling = context.devices().queue(0, K_PROFILING_FLAVOUR);

        USMShared<int> usm(plain, N);
        ASSERT_TRUE(usm);
        int* data = usm.get();
        for (std::size_t i = 0; i < N; ++i)
            data[i] = static_cast<int>(i);

        profiling.submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
            {
                data[idx] = data[idx] * 2;
            });
        }).wait_and_throw();

        for (std::size_t i = 0; i < N; ++i)
            EXPECT_EQ(data[i], static_cast<int>(i) * 2)
                << "element " << i << " was not doubled by the profiling queue's kernel";
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
    } // namespace

    /**
     * @brief Verifies that requesting a queue for an invalid NUMA ID falls back safely instead of
     *        crashing.
     *
     * Requests a queue with a non-existent NUMA ID and validates that the returned fallback queue functions normally.
     */
    TEST(Unit_RuntimeContext, InvalidNUMAFallbackQueue)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        constexpr int INVALID_NUMA_ID = 999;

        EXPECT_NO_THROW({
            sycl::queue& fallback_queue =
                ctx.devices().queue_or_numa_local(std::nullopt, INVALID_NUMA_ID);

            // Confirm the returned queue is usable with a dummy host task.
            auto event = fallback_queue.submit([](sycl::handler& h)
            {
                SushiRuntime::Detail::enqueue_host_task(h, []() {});
            });
            event.wait();
        }) << "Context crashed when an invalid NUMA ID was requested.";
    }

    /**
     * @brief Verifies that an out-of-range NUMA ID falls back to a usable node.
     *
     * A negative id is what a caller gets from a topology that reported no NUMA
     * information, so it has to resolve to something rather than index out of
     * bounds. Allocating and freeing through it proves the fallback queue is a real
     * one and that the pointer's owner was recorded against it.
     */
    TEST(Unit_RuntimeContext, InvalidNUMAFallsBackToAUsableNode)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        constexpr int INVALID_NUMA_ID = -5; // negative edge case

        EXPECT_NO_THROW({
            (void)ctx.devices().queue_or_numa_local(std::nullopt, INVALID_NUMA_ID);
        }) << "Context crashed when asked for a queue on a negative NUMA ID.";

        void* ptr = nullptr;
        EXPECT_NO_THROW({
            ptr = ctx.memory().allocate({Internal::Memory::USMKind::Shared, 128, 0,
                                         ctx.devices().device_for_numa(INVALID_NUMA_ID)});
        }) << "Allocation on a negative NUMA ID threw instead of falling back.";

        ASSERT_NE(ptr, nullptr) << "Fallback allocation returned null.";
        EXPECT_TRUE(ctx.memory().device_of(ptr).has_value())
            << "The fallback allocation was not recorded against any device.";

        ctx.memory().release(ptr);
        EXPECT_FALSE(ctx.memory().device_of(ptr).has_value())
            << "Freeing left the pointer's owner recorded.";
    }

    /** @brief Tests that the live count follows each allocation, free and forgotten record. */
    TEST(Unit_RuntimeContext, LiveUSMCountFollowsAllocateFreeAndForget)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        const std::size_t baseline = ctx.memory().live_count();

        void* shared = ctx.memory().allocate(
            {Internal::Memory::USMKind::Shared, 128, 0, ctx.devices().device_for_numa(0)});
        void* device = ctx.memory().allocate(
            {Internal::Memory::USMKind::Device, 128, 0, ctx.devices().device_for_numa(0)});
        ASSERT_NE(shared, nullptr);
        ASSERT_NE(device, nullptr);
        EXPECT_EQ(ctx.memory().live_count(), baseline + 2);

        ctx.memory().release(shared);
        EXPECT_EQ(ctx.memory().live_count(), baseline + 1);

        ctx.memory().release(device);
        EXPECT_EQ(ctx.memory().live_count(), baseline);

        int external = 0;
        ctx.memory().adopt(&external, 0, sizeof(external));
        EXPECT_EQ(ctx.memory().live_count(), baseline + 1);
        ctx.memory().forget(&external);
        EXPECT_EQ(ctx.memory().live_count(), baseline);
    }

    /** @brief Tests that every aligned allocation returns an address on the requested alignment. */
    TEST(Unit_RuntimeContext, AlignedAllocationsStartOnTheAlignment)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        for (const Internal::Memory::USMKind kind : K_ALIGNED_KINDS)
        {
            for (const std::size_t alignment : {std::size_t{64}, std::size_t{128}})
            {
                for (const std::size_t bytes : {std::size_t{1}, std::size_t{100}, std::size_t{4096}})
                {
                    void* ptr = ctx.memory().allocate({kind, bytes, alignment, 0});
                    ASSERT_NE(ptr, nullptr) << bytes << " bytes at alignment " << alignment;
                    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % alignment, 0u)
                        << bytes << " bytes at alignment " << alignment;
                    ctx.memory().release(ptr);
                }
            }
        }
    }

    /** @brief Tests that an aligned allocation is recorded once and dropped by release(). */
    TEST(Unit_RuntimeContext, AlignedAllocationsAreRecordedAndFreed)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        const std::size_t baseline = ctx.memory().live_count();

        std::array<void*, K_ALIGNED_KIND_COUNT> pointers = {};
        for (std::size_t i = 0; i < K_ALIGNED_KINDS.size(); ++i)
        {
            pointers[i] = ctx.memory().allocate({K_ALIGNED_KINDS[i], 256, 128, 0});
            EXPECT_NE(pointers[i], nullptr);
            EXPECT_EQ(ctx.memory().live_count(), baseline + i + 1);
        }

        for (void* ptr : pointers)
            ctx.memory().release(ptr);
        EXPECT_EQ(ctx.memory().live_count(), baseline);
    }

    /** @brief Tests that an aligned allocation on a device index past the last one stops. */
    TEST(Unit_RuntimeContextDeathTest, AnAlignedAllocationOnAMissingDeviceStops)
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
     * @brief Verifies that an NVIDIA GPU is discovered when CUDA is active.
     *
     * Probes all devices using HardwareDiscovery and asserts that an NVIDIA GPU is found.
     */
    TEST(Unit_RuntimeContextNvidia, NvidiaGPUDiscovered)
    {
        if (!cuda_device_available())
            GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

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
     * Maps NUMA distances for the discovered nodes and asserts that the NVIDIA GPU has a valid ID, skipping if running in WSL without ACPI passthrough.
     */
    TEST(Unit_RuntimeContextNvidia, NvidiaGPUHasNUMAMapping)
    {
        if (!cuda_device_available())
            GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

        HardwareDiscovery discovery;
        DeviceFilter config;
        config.include_gpu = true;

        auto nodes = discovery.probe_all_devices(config).value();
        const HardwareNode* nvidia = find_nvidia_node(nodes);
        ASSERT_NE(nvidia, nullptr);

        NUMAMapper mapper;
        mapper.map_distances(nodes); // mutates nodes in-place, filling numa_node_id

        const HardwareNode* mapped_nvidia = find_nvidia_node(nodes);
        ASSERT_NE(mapped_nvidia, nullptr);

        // -1 means "could not map" — acceptable in WSL without full ACPI passthrough.
        if (mapped_nvidia->numa_node_id == -1)
        {
            GTEST_SKIP() << "GTX 1060 found but hwloc could not resolve NUMA node "
                            "(expected in WSL without PCIe ACPI passthrough). "
                            "NUMA affinity will use fallback (node 0).";
        }

        EXPECT_GE(mapped_nvidia->numa_node_id, 0)
            << "GTX 1060 must be assigned to a valid NUMA node.";
    }

    /**
     * @brief Verifies that the RuntimeContext exposes a functional CUDA-backed SYCL queue.
     *
     * Identifies a CUDA platform queue and executes a trivial host_task to ensure it does not throw an exception.
     */
    TEST(Unit_RuntimeContextNvidia, RuntimeContextExposesNvidiaQueue)
    {
        if (!cuda_device_available())
            GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;

        // Queues are exposed by index in the same order as the SYCL device list.
        auto all_devs = sycl::device::get_devices(sycl::info::device_type::all);
        bool found_cuda_queue = false;
        for (size_t i = 0; i < all_devs.size(); ++i)
        {
            std::string pname = all_devs[i].get_platform().get_info<sycl::info::platform::name>();
            if (pname.find("CUDA") == std::string::npos && pname.find("cuda") == std::string::npos)
                continue;

            found_cuda_queue = true;
            sycl::queue& q = ctx.devices().queue(i);

            EXPECT_NO_THROW({
                auto ev = q.submit([](sycl::handler& h)
                {
                    SushiRuntime::Detail::enqueue_host_task(h, [] {});
                });
                ev.wait();
            }) << "CUDA-backed SYCL queue threw during host_task submission.";

            break;
        }

        if (!found_cuda_queue)
            GTEST_SKIP() << "RuntimeContext found no CUDA-backed queue.";
    }

    /**
     * @brief Performs an end-to-end vector addition smoke test on an NVIDIA GPU.
     *
     * Allocates USM shared memory, submits a parallel_for kernel on the CUDA queue, and verifies the addition results.
     */
    TEST(Unit_RuntimeContextNvidia, NvidiaGPUVectorAdd)
    {
        if (!cuda_device_available())
            GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        sycl::queue* cuda_q = nullptr;
        auto all_devs = sycl::device::get_devices(sycl::info::device_type::all);
        for (size_t i = 0; i < all_devs.size(); ++i)
        {
            std::string pname = all_devs[i].get_platform().get_info<sycl::info::platform::name>();
            if (pname.find("CUDA") != std::string::npos || pname.find("cuda") != std::string::npos)
            {
                cuda_q = &ctx.devices().queue(i);
                break;
            }
        }
        if (!cuda_q)
            GTEST_SKIP() << "No CUDA-backed queue in RuntimeContext.";

        constexpr size_t N = 4096;
        USMShared<float> usm_A(*cuda_q, N);
        USMShared<float> usm_B(*cuda_q, N);
        USMShared<float> usm_C(*cuda_q, N);
        ASSERT_TRUE(usm_A);
        ASSERT_TRUE(usm_B);
        ASSERT_TRUE(usm_C);

        float* A = usm_A.get();
        float* B = usm_B.get();
        float* C = usm_C.get();

        for (size_t i = 0; i < N; ++i)
        {
            A[i] = float(i);
            B[i] = float(i) * 2.f;
            C[i] = 0.f;
        }

        auto ev = cuda_q->submit([&](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
            {
                C[idx] = A[idx] + B[idx];
            });
        });
        ev.wait();

        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            float expected = float(i) + float(i) * 2.f;
            if (C[i] != expected)
            {
                ok = false;
                break;
            }
        }
        EXPECT_TRUE(ok) << "GTX 1060 vector addition produced wrong results.";
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
    TEST(Unit_RuntimeContextTopology, InjectedDistanceModelDrivesRuntimeContext)
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

    /**
     * @brief Verifies end-to-end functionality of synthetic NUMA nodes providing usable queues.
     *
     * Injects a remapping provider and tests that queues obtained for synthetic NUMA IDs can successfully submit host tasks.
     */
    TEST(Unit_RuntimeContextTopology, RuntimeContextRoutesQueuesViaSyntheticNUMA)
    {
        constexpr int FAKE_NODES = 2;

        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), FAKE_NODES);
        auto distance = std::make_shared<MatrixDistanceModel>(
            std::vector<std::vector<int>>{{10, 21},
                                          {21, 10}});

        const auto ctx_owner = SushiRuntime::Test::make_context({}, provider, distance);
        RuntimeContext& ctx = *ctx_owner;

        for (int numa = 0; numa < FAKE_NODES; ++numa)
        {
            EXPECT_NO_THROW({
                sycl::queue& q = ctx.devices().queue_or_numa_local(std::nullopt, numa);
                auto ev = q.submit([](sycl::handler& h) { SushiRuntime::Detail::enqueue_host_task(h, [] {}); });
                ev.wait();
            }) << "Synthetic NUMA node " << numa << " produced no working queue.";
        }
    }

    /**
     * @brief Verifies that a GPU node carrying no PCI address is placed on the fallback NUMA node.
     *
     * Builds a node by hand, leaves its PCI address empty and asserts NUMAMapper puts it on node 0.
     */
    TEST(Unit_RuntimeContextTopology, GPUWithoutPCIAddressTakesFallbackNUMANode)
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

                void prepare(SushiRuntime::span<const sushi_ptr<Node>>) override
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
    TEST(Integration_OffloadSeam, ShutdownStopsCoordinatorBeforeEngine)
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
