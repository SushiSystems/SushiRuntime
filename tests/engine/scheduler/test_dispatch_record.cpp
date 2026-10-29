/**************************************************************************/
/* test_dispatch_record.cpp                                               */
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

/**
 * @file test_dispatch_record.cpp
 * @brief Tests the pairing-claim policy and the polled events of a dispatch, one test on a profiling queue.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/scheduler/dispatch_record.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

/** @brief Tests that nothing claims a pairing with profiling off. */
TEST(Small_DispatchRecord, NothingClaimsWithoutProfiling)
{
    Internal::Graph::Node node;
    EXPECT_FALSE(claims_pairing(false, false, &node));
}

/** @brief Tests that a kernel claims, a host body claims only for a node that is not the sentinel. */
TEST(Small_DispatchRecord, AHostBodyClaimsOnlyForARealNode)
{
    Internal::Graph::Node node;
    EXPECT_TRUE(claims_pairing(true, false, &node));
    EXPECT_TRUE(claims_pairing(true, false, nullptr));
    EXPECT_TRUE(claims_pairing(true, true, &node));
    EXPECT_FALSE(claims_pairing(true, true, nullptr));
    node.is_sentinel = true;
    EXPECT_FALSE(claims_pairing(true, true, &node));
}

/** @brief Tests that a host body returning a default event and handing none over submitted nothing.
 *
 * Two default events need not compare equal (DPC++ gives each its own impl), so the rule must not
 * compare with one.
 */
TEST(Small_DispatchRecord, AnEmptyHostBodySubmittedNothing)
{
    EXPECT_TRUE(submitted_device_work(false, true, sycl::event{}, {}));
    EXPECT_FALSE(submitted_device_work(true, true, sycl::event{}, {}));
    EXPECT_TRUE(submitted_device_work(true, true, sycl::event{}, std::vector<sycl::event>{sycl::event{}}));
}

/** @brief Tests that a host node that ran inline is pushed ready and holds no event. */
TEST(Small_DispatchRecord, AnInlineHostNodeIsReady)
{
    Internal::Graph::Node node;
    const EventPolling::PolledEvent pe = completed_host_event(&node);
    EXPECT_TRUE(pe.ready);
    EXPECT_FALSE(pe.has_event);
    EXPECT_EQ(pe.task, &node);
    EXPECT_EQ(pe.device_index, K_HOST_DEVICE_INDEX);
}

/** @brief Tests that a submission that reached no device is pushed ready, unprofiled with profiling off. */
TEST(Small_DispatchRecord, ASubmissionThatReachedNoDeviceIsReadyAndUnprofiled)
{
    Internal::Graph::Node node;
    const EventPolling::PolledEvent pe =
        dispatched_event(sycl::event{}, false, &node, &node, false, 3, 1, {sycl::event{}});
    EXPECT_TRUE(pe.ready);
    EXPECT_FALSE(pe.has_event);
    EXPECT_EQ(pe.task, &node);
    EXPECT_EQ(pe.step, 3u);
    EXPECT_EQ(pe.device_index, 1u);
    EXPECT_EQ(pe.handed_over.size(), 1u);
    EXPECT_EQ(pe.submit_thread, 0u);
}

/** @brief Tests that a submission that reached a device is pushed waiting on its event. */
TEST(Small_DispatchRecord, ASubmissionThatReachedADeviceWaitsOnItsEvent)
{
    Internal::Graph::Node node;
    const EventPolling::PolledEvent pe = dispatched_event(sycl::event{}, true, &node, &node, false, 0, 0, {});
    EXPECT_FALSE(pe.ready);
    EXPECT_TRUE(pe.has_event);
}

/** @brief Tests that a real event already complete still counts as submitted under profiling.
 *
 * Work this short can finish before the status is read; without its timestamps the run would lose
 * its pairing claim and its device span.
 */
TEST(Medium_DispatchRecord, ACompleteProfiledEventStillCountsAsSubmitted)
{
    sycl::queue queue{sycl::property::queue::enable_profiling{}};
    sycl::event event = queue.submit(
        [](sycl::handler& h) { SushiRuntime::Detail::enqueue_host_task(h, []() {}); });
    event.wait();
    EXPECT_TRUE(submitted_device_work(true, true, event, {}));
    EXPECT_FALSE(submitted_device_work(true, false, event, {})) << "with profiling off a complete event is no work";
}
