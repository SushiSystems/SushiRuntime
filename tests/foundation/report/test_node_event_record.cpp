/**************************************************************************/
/* test_node_event_record.cpp                                             */
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
 * @file test_node_event_record.cpp
 * @brief Tests that NodeEventRecord keeps the size and the field order SushiEngine mirrors.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/report/clock_pairing.hpp>
#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/report/node_event_record.hpp>

using namespace SushiRuntime;

// SushiEngine static_asserts this layout on its side; a change here must reach it first.
static_assert(sizeof(NodeEventRecord) == 48, "NodeEventRecord is 48 bytes");
static_assert(alignof(NodeEventRecord) == 8, "NodeEventRecord is 8-byte aligned");
static_assert(std::is_trivially_copyable_v<NodeEventRecord>, "NodeEventRecord is copied as bytes");
static_assert(offsetof(NodeEventRecord, node_index) == 0, "node_index comes first");
static_assert(offsetof(NodeEventRecord, invocation) == 4, "invocation follows");
static_assert(offsetof(NodeEventRecord, launched_width) == 8, "launched_width follows");
static_assert(offsetof(NodeEventRecord, live_width) == 12, "live_width follows");
static_assert(offsetof(NodeEventRecord, submit_ns) == 16, "submit_ns follows the widths");
static_assert(offsetof(NodeEventRecord, start_ns) == 24, "start_ns follows");
static_assert(offsetof(NodeEventRecord, end_ns) == 32, "end_ns follows");
static_assert(offsetof(NodeEventRecord, device_index) == 40, "device_index is appended after the old fields");
static_assert(offsetof(NodeEventRecord, submit_thread) == 44, "submit_thread is appended last");

/** @brief Tests the size and the order of the record, and what a default record states. */
TEST(Small_NodeEventRecord, HasTheSizeAndTheOrderTheEngineMirrors)
{
    const NodeEventRecord record;

    EXPECT_EQ(sizeof(NodeEventRecord), 48u);
    EXPECT_EQ(record.launched_width, K_WIDTH_UNKNOWN);
    EXPECT_EQ(record.live_width, K_WIDTH_UNKNOWN);
    EXPECT_EQ(record.device_index, K_HOST_DEVICE_INDEX) << "a record names the host until a device claims it";
    EXPECT_EQ(record.submit_thread, 0u);
    EXPECT_EQ(record.submit_ns, 0u);
    EXPECT_EQ(record.start_ns, 0u);
    EXPECT_EQ(record.end_ns, 0u);
}

/** @brief Tests that a device clock holds the device and its pairing as they were given. */
TEST(Small_NodeEventRecord, ADeviceClockKeepsItsDeviceAndPairing)
{
    const DeviceClock clock{2, ClockPairing{100, 200, 5}};

    EXPECT_EQ(clock.device_index, 2u);
    EXPECT_EQ(clock.pairing.event_ns, 100u);
    EXPECT_EQ(clock.pairing.steady_ns, 200u);
    EXPECT_EQ(clock.pairing.uncertainty_ns, 5u);
}
