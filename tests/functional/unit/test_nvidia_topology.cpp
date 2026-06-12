/**************************************************************************/
/* test_nvidia_topology.cpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_nvidia_topology.cpp
 * @brief Validates that an NVIDIA GPU (GTX 1060 / sm_61) is discovered as a
 *        SYCL device via the CUDA backend and correctly mapped to a NUMA node
 *        by hwloc. Tests skip gracefully when no CUDA device is present.
 */

#include <algorithm>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Topology;
using SushiRuntime::Test::UsmShared;

// ---------------------------------------------------------------------------
// Helper: find any NVIDIA GPU among discovered HardwareNodes
// ---------------------------------------------------------------------------
static const HardwareNode* find_nvidia_node(const std::vector<HardwareNode>& nodes)
{
    for (const auto& n : nodes)
    {
        if (n.is_gpu && n.name.find("NVIDIA") != std::string::npos)
            return &n;
        // Codeplay CUDA backend may report "GeForce" without "NVIDIA" prefix
        if (n.is_gpu && (n.name.find("GeForce") != std::string::npos ||
                         n.name.find("GTX")     != std::string::npos ||
                         n.name.find("RTX")     != std::string::npos))
            return &n;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: find any CUDA SYCL device
// ---------------------------------------------------------------------------
static bool cuda_device_available()
{
    for (const auto& dev : sycl::device::get_devices(sycl::info::device_type::gpu))
    {
        // Codeplay CUDA backend reports backend as "cuda" in platform name
        std::string pname = dev.get_platform().get_info<sycl::info::platform::name>();
        if (pname.find("CUDA") != std::string::npos ||
            pname.find("cuda") != std::string::npos)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

/**
 * @test NvidiaGpuDiscovered
 * @brief HardwareDiscovery must enumerate the GTX 1060 as a GPU HardwareNode
 *        when the CUDA backend is active.
 */
TEST(NvidiaTopologyTest, NvidiaGpuDiscovered)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    HardwareDiscovery discovery;
    HardwareFilterConfig config;
    config.include_cpu = true;
    config.include_gpu = true;

    auto nodes = discovery.probe_all_devices(config);
    const HardwareNode* nvidia = find_nvidia_node(nodes);

    ASSERT_NE(nvidia, nullptr) << "NVIDIA GPU not found in discovered hardware nodes.";
    EXPECT_TRUE(nvidia->is_gpu);
    EXPECT_FALSE(nvidia->name.empty());
}

/**
 * @test NvidiaGpuHasNUMAMapping
 * @brief After discovery the GTX 1060 must have a valid NUMA node id (>= 0)
 *        assigned by NUMAMapper via hwloc PCI topology walk.
 */
TEST(NvidiaTopologyTest, NvidiaGpuHasNUMAMapping)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    HardwareDiscovery discovery;
    HardwareFilterConfig config;
    config.include_gpu = true;

    auto nodes = discovery.probe_all_devices(config);
    const HardwareNode* nvidia = find_nvidia_node(nodes);
    ASSERT_NE(nvidia, nullptr);

    NUMAMapper mapper;
    // map_distances() mutates nodes in-place, filling numa_node_id
    mapper.map_distances(nodes);

    const HardwareNode* mapped_nvidia = find_nvidia_node(nodes);
    ASSERT_NE(mapped_nvidia, nullptr);

    // -1 means "could not map" — acceptable in WSL without full ACPI passthrough
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
 * @test RuntimeContextExposesNvidiaQueue
 * @brief RuntimeContext must provide a valid SYCL queue backed by the CUDA device
 *        and the queue must successfully execute a trivial host_task.
 */
TEST(NvidiaTopologyTest, RuntimeContextExposesNvidiaQueue)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    RuntimeContext ctx;

    // RuntimeContext exposes queues by index; find the CUDA-backed one by
    // scanning SYCL devices directly (same order as ctx.get_queue(i)).
    auto all_devs = sycl::device::get_devices(sycl::info::device_type::all);
    bool found_cuda_queue = false;
    for (size_t i = 0; i < all_devs.size(); ++i)
    {
        std::string pname = all_devs[i].get_platform().get_info<sycl::info::platform::name>();
        if (pname.find("CUDA") == std::string::npos && pname.find("cuda") == std::string::npos)
            continue;

        found_cuda_queue = true;
        sycl::queue& q = ctx.get_queue(i);

        EXPECT_NO_THROW({
            auto ev = q.submit([](sycl::handler& h)
            {
                h.host_task([]{ /* no-op */ });
            });
            ev.wait();
        }) << "CUDA-backed SYCL queue threw during host_task submission.";

        break;
    }

    if (!found_cuda_queue)
        GTEST_SKIP() << "RuntimeContext found no CUDA-backed queue.";
}

/**
 * @test NvidiaGpuVectorAdd
 * @brief End-to-end smoke test: allocate USM shared memory, run a parallel_for
 *        on the GTX 1060 via the CUDA SYCL backend, verify results on host.
 *        This confirms the full pipeline: CUDA backend → kernel dispatch → USM.
 */
TEST(NvidiaTopologyTest, NvidiaGpuVectorAdd)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    // Find the CUDA queue from RuntimeContext
    RuntimeContext ctx;
    sycl::queue* cuda_q = nullptr;
    auto all_devs = sycl::device::get_devices(sycl::info::device_type::all);
    for (size_t i = 0; i < all_devs.size(); ++i)
    {
        std::string pname = all_devs[i].get_platform().get_info<sycl::info::platform::name>();
        if (pname.find("CUDA") != std::string::npos || pname.find("cuda") != std::string::npos)
        {
            cuda_q = &ctx.get_queue(i);
            break;
        }
    }
    if (!cuda_q)
        GTEST_SKIP() << "No CUDA-backed queue in RuntimeContext.";

    constexpr size_t N = 4096;
    UsmShared<float> usm_A(*cuda_q, N);
    UsmShared<float> usm_B(*cuda_q, N);
    UsmShared<float> usm_C(*cuda_q, N);
    ASSERT_TRUE(usm_A); ASSERT_TRUE(usm_B); ASSERT_TRUE(usm_C);

    float* A = usm_A.get();
    float* B = usm_B.get();
    float* C = usm_C.get();

    // Initialize on host (USM shared = accessible from both sides)
    for (size_t i = 0; i < N; ++i) { A[i] = float(i); B[i] = float(i) * 2.f; C[i] = 0.f; }

    // Dispatch kernel to GTX 1060 via CUDA backend
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
        if (C[i] != expected) { ok = false; break; }
    }
    EXPECT_TRUE(ok) << "GTX 1060 vector addition produced wrong results.";
}
