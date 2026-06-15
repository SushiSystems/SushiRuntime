/**************************************************************************/
/* test_nvidia_topology.cpp                                               */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Validates that an NVIDIA GPU (GTX 1060 / sm_61) is discovered via the CUDA
// SYCL backend and mapped to a NUMA node by hwloc. Skips when no CUDA device.

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

// Find any NVIDIA GPU among discovered nodes (the CUDA backend may report
// "GeForce"/"GTX"/"RTX" instead of "NVIDIA").
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

// True if any GPU device sits on a CUDA platform.
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

// Discovery must enumerate the GTX 1060 as a GPU node when CUDA is active.
TEST(Unit_NvidiaTopologyTest, NvidiaGpuDiscovered)
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

// After discovery the GPU must have a valid NUMA id (>= 0), or skip on WSL.
TEST(Unit_NvidiaTopologyTest, NvidiaGpuHasNUMAMapping)
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

// RuntimeContext must expose a CUDA-backed queue that runs a trivial host_task.
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
        sycl::queue& q = ctx.get_queue(i);

        EXPECT_NO_THROW({
            auto ev = q.submit([](sycl::handler& h)
            {
                h.host_task([] {});
            });
            ev.wait();
        }) << "CUDA-backed SYCL queue threw during host_task submission.";

        break;
    }

    if (!found_cuda_queue)
        GTEST_SKIP() << "RuntimeContext found no CUDA-backed queue.";
}

// End-to-end smoke test: USM shared + a parallel_for on the GTX 1060.
TEST(Unit_NvidiaTopologyTest, NvidiaGpuVectorAdd)
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
