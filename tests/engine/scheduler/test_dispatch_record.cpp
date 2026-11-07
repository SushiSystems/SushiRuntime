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

/**
 * @file test_dispatch_record.cpp
 * @brief Tests the pairing-claim policy and the polled events of a dispatch, two tests on a queue.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/scheduler/dispatch_record.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief The node a dispatch names, if any. */
    enum class NodeKind
    {
        /** @brief No node: a null pointer. */
        None,
        /** @brief A node of the graph. */
        Real,
        /** @brief The sentinel node that stands for no work. */
        Sentinel
    };

    /** @brief One call of claims_pairing and the answer it must give. */
    struct ClaimRow
    {
        /** @brief Whether profiling is on. */
        bool profiling;
        /** @brief Whether the body is a host body. */
        bool host;
        /** @brief The node the dispatch names. */
        NodeKind node;
        /** @brief Whether the dispatch claims its pairing. */
        bool claims;
    };

    /** @brief Checks that claims_pairing answers every row. */
    void expect_claims(const std::vector<ClaimRow>& rows)
    {
        Internal::Graph::Node real;
        Internal::Graph::Node sentinel;
        sentinel.is_sentinel = true;
        for (const ClaimRow& row : rows)
        {
            const Internal::Graph::Node* node = nullptr;
            if (row.node == NodeKind::Real)
                node = &real;
            if (row.node == NodeKind::Sentinel)
                node = &sentinel;
            SCOPED_TRACE(::testing::Message() << "profiling " << row.profiling << ", host " << row.host
                                              << ", node " << static_cast<int>(row.node));
            EXPECT_EQ(claims_pairing(row.profiling, row.host, node), row.claims);
        }
    }

    /** @brief One call of submitted_device_work with a default event and the answer it must give. */
    struct SubmissionRow
    {
        /** @brief Whether the body is a host body. */
        bool host;
        /** @brief Whether profiling is on. */
        bool profiling;
        /** @brief How many events the body handed over. */
        std::size_t handed_over;
        /** @brief Whether the submission counts as device work. */
        bool submitted;
    };

    /** @brief Checks that submitted_device_work answers every row for a default event. */
    void expect_submitted(const std::vector<SubmissionRow>& rows)
    {
        for (const SubmissionRow& row : rows)
        {
            SCOPED_TRACE(::testing::Message() << "host " << row.host << ", profiling " << row.profiling
                                              << ", handed over " << row.handed_over);
            EXPECT_EQ(submitted_device_work(row.host, row.profiling, sycl::event{},
                                            std::vector<sycl::event>(row.handed_over)),
                      row.submitted);
        }
    }

    /** @brief Builds the event of a submission that reached no device, with profiling off. */
    EventPolling::PolledEvent unprofiled_submission(Internal::Graph::Node& node)
    {
        return dispatched_event(sycl::event{}, false, &node, &node, false, 3, 1, {sycl::event{}});
    }
} // namespace

/** @brief Fixture of the tests that submit to a queue of the program. */
class Medium_DispatchRecord : public TestSupport::RuntimeTest
{
    protected:
        /** @brief Returns the one queue of the fixture that profiles, on the device of the program's queue. */
        static sycl::queue& profiling_queue()
        {
            static sycl::queue profiled{queue().get_context(), queue().get_device(),
                                        sycl::property::queue::enable_profiling{}};
            return profiled;
        }
};

/** @brief Tests that nothing claims a pairing with profiling off. */
TEST(Small_DispatchRecord, NothingClaimsWithoutProfiling)
{
    expect_claims({{false, false, NodeKind::Real, false},
                   {false, false, NodeKind::None, false},
                   {false, true, NodeKind::Real, false},
                   {false, true, NodeKind::None, false},
                   {false, true, NodeKind::Sentinel, false}});
}

/** @brief Tests that, under profiling, a kernel claims and a host body claims only for a node that is real. */
TEST(Small_DispatchRecord, AHostBodyClaimsOnlyForARealNode)
{
    expect_claims({{true, false, NodeKind::Real, true},
                   {true, false, NodeKind::None, true},
                   {true, true, NodeKind::Real, true},
                   {true, true, NodeKind::None, false},
                   {true, true, NodeKind::Sentinel, false}});
}

/** @brief Tests that a submission counts as device work when it is a kernel or hands an event over.
 *
 * Two default events need not compare equal (DPC++ gives each its own impl), so the rule must not
 * compare with one.
 */
TEST(Small_DispatchRecord, AnEmptyHostBodySubmittedNothing)
{
    expect_submitted({{false, true, 0, true}, {true, true, 0, false}, {true, true, 1, true}});
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

/** @brief Tests that a submission that reached no device is pushed ready and holds no event. */
TEST(Small_DispatchRecord, ASubmissionThatReachedNoDeviceIsReady)
{
    Internal::Graph::Node node;
    const EventPolling::PolledEvent pe = unprofiled_submission(node);
    EXPECT_TRUE(pe.ready);
    EXPECT_FALSE(pe.has_event);
}

/** @brief Tests that a submission that reached no device still carries its task, step, device and handed-over events. */
TEST(Small_DispatchRecord, ASubmissionThatReachedNoDeviceCarriesItsStepAndDevice)
{
    Internal::Graph::Node node;
    const EventPolling::PolledEvent pe = unprofiled_submission(node);
    EXPECT_EQ(pe.task, &node);
    EXPECT_EQ(pe.step, 3u);
    EXPECT_EQ(pe.device_index, 1u);
    EXPECT_EQ(pe.handed_over.size(), 1u);
}

/** @brief Tests that an unprofiled submission records no submitting thread. */
TEST(Small_DispatchRecord, AnUnprofiledSubmissionRecordsNoSubmittingThread)
{
    Internal::Graph::Node node;
    EXPECT_EQ(unprofiled_submission(node).submit_thread, 0u);
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
TEST_F(Medium_DispatchRecord, ACompleteProfiledEventStillCountsAsSubmitted)
{
    sycl::event event = profiling_queue().submit(
        [](sycl::handler& h) { SushiRuntime::Detail::enqueue_host_task(h, []() {}); });
    event.wait();
    EXPECT_TRUE(submitted_device_work(true, true, event, {}));
}

/** @brief Tests that with profiling off a complete event is no device work. */
TEST(Small_DispatchRecord, ACompleteEventIsNoWorkWithProfilingOff)
{
    EXPECT_FALSE(submitted_device_work(true, false, sycl::event{}, {}));
}
