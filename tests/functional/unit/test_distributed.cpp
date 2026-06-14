/**************************************************************************/
/* test_distributed.cpp                                                   */
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
 * @file test_distributed.cpp
 * @brief Hardware-free tests for the distributed layer: wire-protocol
 *        serialization, the in-process transport switchboard (the single-machine
 *        simulation of a real cluster), and the master-side registries. These
 *        exercise the full master/worker message path without needing 5 machines
 *        or SYCL devices — the same role the RemappingTopologyProvider plays for
 *        NUMA. End-to-end kernel offload (which needs SYCL + a RuntimeContext) is
 *        covered separately as an integration test.
 */

#include <future>
#include <chrono>
#include <vector>
#include <cstring>
#include <gtest/gtest.h>

#include <SushiRuntime/distributed/protocol.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/buffer_registry.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>
#include <SushiRuntime/graph/node.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace SushiRuntime::Graph::Literals;

namespace
{
    std::span<const std::byte> as_bytes(const std::vector<std::byte>& v)
    {
        return { v.data(), v.size() };
    }
}

/**
 * @test ProtocolRoundTrip
 * @brief A TaskDispatchMsg carrying metadata and handle lists survives
 *        serialize -> deserialize byte-for-byte. This is the contract that lets
 *        a compiled-in kernel reference (OpID) and data references (handles)
 *        cross the wire while the code and pointers never do.
 */
TEST(Unit_DistributedTest, ProtocolRoundTrip)
{
    TaskDispatchMsg msg;
    msg.task_id = 42;
    msg.op_id   = "blas.gemm"_op;
    msg.meta.name = "gemm";
    msg.meta.priority = 7;
    msg.meta.set_param<double>(0, 2.5);
    msg.inputs.push_back(BufferHandle{ GlobalBufferID{1}, 3, 256 });
    msg.inputs.push_back(BufferHandle{ GlobalBufferID{2}, 1, 512 });
    msg.outputs.push_back(BufferHandle{ GlobalBufferID{3}, 1, 128 });

    auto bytes = msg.serialize();
    auto back  = TaskDispatchMsg::deserialize(as_bytes(bytes));

    EXPECT_EQ(back.task_id, 42u);
    EXPECT_EQ(back.op_id.value, ("blas.gemm"_op).value);
    EXPECT_EQ(back.meta.priority, 7);
    EXPECT_DOUBLE_EQ(back.meta.get_param<double>(0), 2.5);
    ASSERT_EQ(back.inputs.size(), 2u);
    EXPECT_EQ(back.inputs[0].id.value, 1u);
    EXPECT_EQ(back.inputs[0].version, 3u);
    EXPECT_EQ(back.inputs[1].size_bytes, 512u);
    ASSERT_EQ(back.outputs.size(), 1u);
    EXPECT_EQ(back.outputs[0].id.value, 3u);
}

/**
 * @test BufferPushRoundTrip
 * @brief The raw-bytes payload (the actual data pull) preserves its blob.
 */
TEST(Unit_DistributedTest, BufferPushRoundTrip)
{
    BufferPushMsg push;
    push.handle = BufferHandle{ GlobalBufferID{9}, 5, 4 };
    push.data   = { std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF} };

    auto bytes = push.serialize();
    auto back  = BufferPushMsg::deserialize(as_bytes(bytes));

    EXPECT_EQ(back.handle.id.value, 9u);
    EXPECT_EQ(back.handle.version, 5u);
    ASSERT_EQ(back.data.size(), 4u);
    EXPECT_EQ(back.data[3], std::byte{0xEF});
}

/**
 * @test InProcessTransportDeliversBothDirections
 * @brief master -> worker and worker -> master messages arrive on the peer's
 *        handler with the correct peer id, proving the switchboard routes a real
 *        multi-node protocol on a single machine.
 */
TEST(Unit_DistributedTest, InProcessTransportDeliversBothDirections)
{
    InProcessFabric fabric(/*worker_count=*/1);
    InProcessTransport master(fabric, /*node_id=*/-1);
    InProcessTransport worker(fabric, /*node_id=*/0);

    std::promise<std::pair<int, MsgType>> worker_got;
    std::promise<std::pair<int, MsgType>> master_got;
    auto worker_fut = worker_got.get_future();
    auto master_fut = master_got.get_future();

    worker.on_message([&](int peer, MsgType type, std::span<const std::byte>)
                      { worker_got.set_value({ peer, type }); });
    master.on_message([&](int peer, MsgType type, std::span<const std::byte>)
                      { master_got.set_value({ peer, type }); });

    master.start();
    worker.start();

    std::vector<std::byte> empty;
    master.send(0, MsgType::TASK_DISPATCH, as_bytes(empty));
    worker.send(0, MsgType::TASK_RESULT,   as_bytes(empty));

    ASSERT_EQ(worker_fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_EQ(master_fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto [wpeer, wtype] = worker_fut.get();
    auto [mpeer, mtype] = master_fut.get();

    EXPECT_EQ(wpeer, -1);                       // worker sees the master
    EXPECT_EQ(wtype, MsgType::TASK_DISPATCH);
    EXPECT_EQ(mpeer, 0);                         // master sees worker 0
    EXPECT_EQ(mtype, MsgType::TASK_RESULT);

    worker.stop();
    master.stop();
}

/**
 * @test BufferRegistryVersioning
 * @brief Registration is stable per pointer; a write bumps the version (the
 *        invalidation signal); lookup_by_id round-trips id -> pointer.
 */
TEST(Unit_DistributedTest, BufferRegistryVersioning)
{
    BufferRegistry reg;
    int storage = 0;
    void* p = &storage;

    auto h1 = reg.register_buffer(p, sizeof(storage));
    EXPECT_TRUE(h1.id.valid());
    EXPECT_EQ(h1.version, 1u);

    // Re-registering the same pointer is idempotent.
    auto h1b = reg.register_buffer(p, sizeof(storage));
    EXPECT_EQ(h1b.id.value, h1.id.value);

    reg.bump_version(p);
    auto looked = reg.lookup(p);
    ASSERT_TRUE(looked.has_value());
    EXPECT_EQ(looked->version, 2u);

    auto by_id = reg.lookup_by_id(h1.id);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->ptr, p);
    EXPECT_EQ(by_id->handle.version, 2u);

    EXPECT_FALSE(reg.lookup(nullptr).has_value());
}

// ---------------------------------------------------------------------------
// Offload policy (LeastLoadedOffloadPolicy)
//
// These drive pick_worker() directly with synthetic WorkerStats — the same
// hardware-free style the transport tests use — so the cost model (rtt +
// transfer + queue backlog) and its gates are validated without a live cluster.
// ---------------------------------------------------------------------------
namespace
{
    WorkerStats live_worker(int id, uint32_t inflight, uint64_t free_mem, uint32_t rtt_us)
    {
        WorkerStats w;
        w.worker_id  = id;
        w.alive      = true;
        w.inflight   = inflight;
        w.free_mem   = free_mem;
        w.rtt_micros = rtt_us;
        return w;
    }

    constexpr uint64_t BIG_PAYLOAD = 1u << 20; // 1 MiB, well over the 64 KiB gate
    constexpr uint64_t AMPLE_MEM   = 1ull << 30; // 1 GiB headroom
}

/**
 * @test PolicyKeepsTinyPayloadLocal
 * @brief A payload below min_offload_bytes never offloads — the network would
 *        only add latency — even with an idle, healthy worker available.
 */
TEST(Unit_DistributedTest, PolicyKeepsTinyPayloadLocal)
{
    LeastLoadedOffloadPolicy policy; // default 64 KiB gate
    Graph::Node node;
    std::vector<WorkerStats> workers{ live_worker(0, /*inflight=*/0, AMPLE_MEM, /*rtt=*/50) };

    auto pick = policy.pick_worker(node, workers, /*est_bytes=*/1024);
    EXPECT_FALSE(pick.has_value());
}

/**
 * @test PolicyPicksLowerRttWorker
 * @brief With equal load and headroom, the nearer (lower-RTT) worker wins —
 *        proving rtt_micros is actually consumed by the cost model.
 */
TEST(Unit_DistributedTest, PolicyPicksLowerRttWorker)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, /*inflight=*/0, AMPLE_MEM, /*rtt=*/50000), // far
        live_worker(1, /*inflight=*/0, AMPLE_MEM, /*rtt=*/100),   // near
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 1);
}

/**
 * @test PolicyPrefersLessLoadedWhenEquallyNear
 * @brief Equal RTT and headroom: the emptier queue wins (the "least loaded"
 *        intent survives inside the richer cost model).
 */
TEST(Unit_DistributedTest, PolicyPrefersLessLoadedWhenEquallyNear)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, /*inflight=*/5, AMPLE_MEM, /*rtt=*/200),
        live_worker(1, /*inflight=*/1, AMPLE_MEM, /*rtt=*/200),
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 1);
}

/**
 * @test PolicyRejectsInsufficientHeadroom
 * @brief A worker reporting less free_mem than the payload is excluded; with no
 *        other candidate the task falls back to local (nullopt).
 */
TEST(Unit_DistributedTest, PolicyRejectsInsufficientHeadroom)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, /*inflight=*/0, /*free_mem=*/BIG_PAYLOAD / 2, /*rtt=*/100),
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    EXPECT_FALSE(pick.has_value());
}

/**
 * @test PolicyFallsBackLocalWhenNoLiveWorker
 * @brief No live worker -> transparent local fallback, regardless of payload.
 */
TEST(Unit_DistributedTest, PolicyFallsBackLocalWhenNoLiveWorker)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    WorkerStats dead = live_worker(0, 0, AMPLE_MEM, 100);
    dead.alive = false;
    std::vector<WorkerStats> workers{ dead };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    EXPECT_FALSE(pick.has_value());
}

/**
 * @test PolicyTreatsZeroFreeMemAsUnknown
 * @brief free_mem == 0 means "no headroom info" (e.g. the device lacks a
 *        free-memory query), so the worker stays eligible rather than excluded.
 */
TEST(Unit_DistributedTest, PolicyTreatsZeroFreeMemAsUnknown)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{ live_worker(0, /*inflight=*/0, /*free_mem=*/0, /*rtt=*/100) };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 0);
}
