/**************************************************************************/
/* test_distributed.cpp                                                   */
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

// Hardware-free tests for the distributed layer: wire-protocol serialization,
// the in-process transport switchboard (single-machine cluster simulation), and
// the master-side registries/policy. End-to-end kernel offload is covered by an
// integration test.

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
    /**
     * @brief Converts a byte vector to a read-only span.
     *
     * @param v The byte vector.
     * @return A span covering the vector's data.
     */
    SushiRuntime::span<const std::byte> as_bytes(const std::vector<std::byte>& v)
    {
        return { v.data(), v.size() };
    }
}

/**
 * @brief Tests that a TaskDispatchMsg survives serialization/deserialization byte-for-byte.
 *
 * Verifies the contract that lets OpID and handles cross the wire while the
 * code and pointers never do.
 */
TEST(Unit_DistributedTest, ProtocolRoundTrip)
{
    TaskDispatchMsg msg;
    msg.task_id       = 42;
    msg.op_id         = "blas.gemm"_op;
    msg.meta.name     = "gemm";
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
 * @brief Tests that the raw-bytes payload of a buffer push preserves its blob.
 *
 * Checks that BufferPushMsg serialization maintains the byte data and handle.
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
 * @brief Tests that in-process transport routes messages correctly in both directions.
 *
 * master->worker and worker->master messages arrive on the peer's handler with
 * the correct peer id, simulating a real protocol on one machine.
 */
TEST(Unit_DistributedTest, InProcessTransportDeliversBothDirections)
{
    InProcessFabric fabric(1);
    InProcessTransport master(fabric, -1);
    InProcessTransport worker(fabric, 0);

    std::promise<std::pair<int, MsgType>> worker_got;
    std::promise<std::pair<int, MsgType>> master_got;
    auto worker_fut = worker_got.get_future();
    auto master_fut = master_got.get_future();

    worker.on_message([&](int peer, MsgType type, SushiRuntime::span<const std::byte>)
    {
        worker_got.set_value({ peer, type });
    });
    master.on_message([&](int peer, MsgType type, SushiRuntime::span<const std::byte>)
    {
        master_got.set_value({ peer, type });
    });

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
 * @brief Tests buffer registration stability and version bumping.
 *
 * Registration is stable per pointer; re-registration bumps the version
 * (USM address reuse detection); a write bump increments further; lookup_by_id
 * round-trips id -> pointer.
 */
TEST(Unit_DistributedTest, BufferRegistryVersioning)
{
    BufferRegistry reg;
    int storage = 0;
    void* p = &storage;

    auto h1 = reg.register_buffer(p, sizeof(storage));
    EXPECT_TRUE(h1.id.valid());
    EXPECT_EQ(h1.version, 1u);

    // Re-registering the same pointer bumps the version (stale-handle detection
    // for USM address reuse) but preserves the existing id.
    auto h1b = reg.register_buffer(p, sizeof(storage));
    EXPECT_EQ(h1b.id.value, h1.id.value);
    EXPECT_EQ(h1b.version, 2u);

    reg.bump_version(p);
    auto looked = reg.lookup(p);
    ASSERT_TRUE(looked.has_value());
    EXPECT_EQ(looked->version, 3u);

    auto by_id = reg.lookup_by_id(h1.id);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->ptr, p);
    EXPECT_EQ(by_id->handle.version, 3u);

    EXPECT_FALSE(reg.lookup(nullptr).has_value());
}

// Offload policy (LeastLoadedOffloadPolicy): drive pick_worker() with synthetic
// WorkerStats so the cost model (rtt + transfer + queue backlog) and its gates
// are validated without a live cluster.
namespace
{
    /**
     * @brief Helper to create synthetic WorkerStats for testing.
     *
     * @param id The worker ID.
     * @param inflight Number of in-flight tasks.
     * @param free_mem Available free memory in bytes.
     * @param rtt_us Round-trip time in microseconds.
     * @return A WorkerStats object.
     */
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

    constexpr uint64_t BIG_PAYLOAD = 1u << 20;   // 1 MiB, well over the 64 KiB gate
    constexpr uint64_t AMPLE_MEM   = 1ull << 30; // 1 GiB headroom
}

/**
 * @brief Tests that a payload below min_offload_bytes never offloads.
 *
 * Even with an idle worker, the payload is kept local if it is too small.
 */
TEST(Unit_DistributedTest, PolicyKeepsTinyPayloadLocal)
{
    LeastLoadedOffloadPolicy policy; // default 64 KiB gate
    Graph::Node node;
    std::vector<WorkerStats> workers{ live_worker(0, 0, AMPLE_MEM, 50) };

    auto pick = policy.pick_worker(node, workers, 1024);
    EXPECT_FALSE(pick.has_value());
}

/**
 * @brief Tests that the nearer worker is picked when load and headroom are equal.
 *
 * The policy prefers the worker with the lower round-trip time (RTT).
 */
TEST(Unit_DistributedTest, PolicyPicksLowerRttWorker)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, 0, AMPLE_MEM, 50000), // far
        live_worker(1, 0, AMPLE_MEM, 100),   // near
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 1);
}

/**
 * @brief Tests that the emptier queue wins when RTT and headroom are equal.
 *
 * Prefers the worker with fewer in-flight tasks.
 */
TEST(Unit_DistributedTest, PolicyPrefersLessLoadedWhenEquallyNear)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, 5, AMPLE_MEM, 200),
        live_worker(1, 1, AMPLE_MEM, 200),
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 1);
}

/**
 * @brief Tests that a worker with less free memory than the payload is excluded.
 *
 * If no candidate has sufficient headroom, the task falls back to local execution.
 */
TEST(Unit_DistributedTest, PolicyRejectsInsufficientHeadroom)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{
        live_worker(0, 0, BIG_PAYLOAD / 2, 100),
    };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    EXPECT_FALSE(pick.has_value());
}

/**
 * @brief Tests that execution falls back to local when there are no live workers.
 *
 * Ensures transparent local fallback.
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
 * @brief Tests that zero free memory means "no headroom info", keeping the worker eligible.
 *
 * The policy treats free_mem == 0 as unknown rather than out of memory.
 */
TEST(Unit_DistributedTest, PolicyTreatsZeroFreeMemAsUnknown)
{
    LeastLoadedOffloadPolicy policy;
    Graph::Node node;
    std::vector<WorkerStats> workers{ live_worker(0, 0, 0, 100) };

    auto pick = policy.pick_worker(node, workers, BIG_PAYLOAD);
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(*pick, 0);
}
