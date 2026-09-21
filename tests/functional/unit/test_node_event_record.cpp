/**************************************************************************/
/* test_node_event_record.cpp                                             */
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

// Unit coverage for Core::NodeEventRecord's layout and for RunReport::reset()
// keeping node_events' capacity while zeroing the clock pairing. The engine
// side copies NodeEventRecord field-for-field with no translation, so its
// size and triviality are pinned down here rather than left to drift.

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/core/run_report.hpp>

using namespace SushiRuntime;

// 40 bytes, trivially copyable: the shape the engine backend copies with no
// translation. A padding change or an added field here breaks that contract.
TEST(Unit_NodeEventRecord, LayoutIsFortyBytesAndTriviallyCopyable)
{
    static_assert(sizeof(Core::NodeEventRecord) == 40u, "NodeEventRecord must stay 40 bytes");
    static_assert(std::is_trivially_copyable<Core::NodeEventRecord>::value,
                  "NodeEventRecord must stay trivially copyable");
    EXPECT_EQ(sizeof(Core::NodeEventRecord), 40u);
    EXPECT_TRUE(std::is_trivially_copyable<Core::NodeEventRecord>::value);
}

// reset() must not free node_events' storage, matching the retained-capacity
// contract every other RunReport vector already carries.
TEST(Unit_NodeEventRecord, ResetKeepsNodeEventsCapacity)
{
    Core::RunReport report;
    report.node_events.push_back(Core::NodeEventRecord{1, 0, Core::WIDTH_UNKNOWN, Core::WIDTH_UNKNOWN, 10, 20, 30});
    report.node_events.push_back(Core::NodeEventRecord{2, 0, Core::WIDTH_UNKNOWN, Core::WIDTH_UNKNOWN, 40, 50, 60});
    const std::size_t capacity_before = report.node_events.capacity();

    report.reset();

    EXPECT_TRUE(report.node_events.empty());
    EXPECT_EQ(report.node_events.capacity(), capacity_before);
}

// reset() must zero the clock pairing, so a stale pairing from a previous run
// never survives into a report that reads as freshly reset.
TEST(Unit_NodeEventRecord, ResetZeroesClockPairing)
{
    Core::RunReport report;
    report.clock = Core::ClockPairing{100, 200, 5};

    report.reset();

    EXPECT_EQ(report.clock.event_ns, 0u);
    EXPECT_EQ(report.clock.steady_ns, 0u);
    EXPECT_EQ(report.clock.uncertainty_ns, 0u);
}

// The widths default to WIDTH_UNKNOWN rather than zero, because zero is a real
// dispatch width on an empty pass and would read as a measured value.
TEST(Unit_NodeEventRecord, WidthsDefaultToUnknown)
{
    const Core::NodeEventRecord rec{};
    EXPECT_EQ(Core::WIDTH_UNKNOWN, 0xFFFFFFFFu);
    EXPECT_EQ(rec.launched_width, Core::WIDTH_UNKNOWN);
    EXPECT_EQ(rec.live_width, Core::WIDTH_UNKNOWN);
}
