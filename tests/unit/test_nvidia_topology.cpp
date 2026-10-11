/**************************************************************************/
/* test_nvidia_topology.cpp                                               */
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
 * @file test_nvidia_topology.cpp
 * @brief Tests that an NVIDIA GPU is discovered through the CUDA backend and mapped to a NUMA node.
 * @see tests/unit/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
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
TEST(Unit_NvidiaTopologyTest, NvidiaGPUDiscovered)
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
 * @brief Verifies that a discovered NVIDIA GPU is assigned a valid NUMA node ID.
 *
 * Maps NUMA distances for the discovered nodes and asserts that the NVIDIA GPU has a valid ID, skipping if running in WSL without ACPI passthrough.
 */
TEST(Unit_NvidiaTopologyTest, NvidiaGPUHasNUMAMapping)
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
TEST(Unit_NvidiaTopologyTest, RuntimeContextExposesNvidiaQueue)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    RuntimeContext ctx;

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
                SushiRuntime::detail::enqueue_host_task(h, [] {});
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
TEST(Unit_NvidiaTopologyTest, NvidiaGPUVectorAdd)
{
    if (!cuda_device_available())
        GTEST_SKIP() << "No CUDA-capable GPU found — skipping NVIDIA topology tests.";

    RuntimeContext ctx;
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
