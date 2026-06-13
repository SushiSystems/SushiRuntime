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
TEST(DistributedTest, ProtocolRoundTrip)
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
TEST(DistributedTest, BufferPushRoundTrip)
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
TEST(DistributedTest, InProcessTransportDeliversBothDirections)
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
TEST(DistributedTest, BufferRegistryVersioning)
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
