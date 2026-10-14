/**************************************************************************/
/* test_cluster.cpp                                                       */
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
 * @file test_cluster.cpp
 * @brief Tests Cluster without a worker: attach, shared buffers, versions and the option checks.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/transport/buffer_handle.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a master with no worker reports none and offloads nothing. */
TEST(Integration_Cluster, AMasterWithNoWorkerReportsNone)
{
    auto runtime = Runtime::create().value();
    InProcessFabric fabric;
    Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();
    EXPECT_EQ(cluster.alive_workers(), 0u);
    EXPECT_FALSE(cluster.wait_for_workers(1, std::chrono::milliseconds(0)));
    EXPECT_EQ(cluster.statistics().alive_workers, 0u);
    EXPECT_EQ(cluster.offloaded(), 0u);
    cluster.detach();
    EXPECT_EQ(cluster.alive_workers(), 0u);
}

/** @brief Tests that a shared address gets a valid handle whose version bump_buffer() raises. */
TEST(Integration_Cluster, ASharedAddressGetsAHandle)
{
    auto runtime = Runtime::create().value();
    InProcessFabric fabric;
    Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();
    auto buffer = runtime.buffer<float>(16).value();
    cluster.share(buffer);

    const std::optional<BufferHandle> handle = cluster.buffer_handle(buffer.data());
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->id.valid());
    EXPECT_EQ(handle->size_bytes, 16 * sizeof(float));
    cluster.bump_buffer(buffer.data());
    EXPECT_EQ(cluster.buffer_handle(buffer.data())->version, handle->version + 1);

    int unknown = 0;
    EXPECT_FALSE(cluster.buffer_handle(&unknown).has_value());
    cluster.detach();
}

/** @brief Tests that a liveness timeout under one millisecond is refused and changes nothing. */
TEST(Integration_Cluster, ALivenessTimeoutBelowTheMinimumIsRefused)
{
    auto runtime = Runtime::create().value();
    InProcessFabric fabric;
    Cluster cluster = Cluster::attach(runtime, InProcessTransport::make_master(fabric)).value();
    EXPECT_FALSE(cluster.liveness_timeout(std::chrono::milliseconds(0)).has_value());
    EXPECT_TRUE(cluster.liveness_timeout(K_MINIMUM_TIMEOUT).has_value());
    cluster.detach();
}
