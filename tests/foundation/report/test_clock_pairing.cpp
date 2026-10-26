/**************************************************************************/
/* test_clock_pairing.cpp                                                 */
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
 * @file test_clock_pairing.cpp
 * @brief Tests ClockPairing and DeviceClock, which place a device's event clock on the steady clock.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/report/clock_pairing.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default pairing is the identity pairing: both readings 0 and no uncertainty. */
TEST(Small_ClockPairing, ADefaultPairingIsTheIdentity)
{
    const ClockPairing pairing;

    EXPECT_EQ(pairing.event_ns, 0u);
    EXPECT_EQ(pairing.steady_ns, 0u);
    EXPECT_EQ(pairing.uncertainty_ns, 0u);
}

/** @brief Tests that a pairing keeps its three readings as given. */
TEST(Small_ClockPairing, APairingKeepsItsThreeReadings)
{
    const ClockPairing pairing{100, 200, 5};

    EXPECT_EQ(pairing.event_ns, 100u);
    EXPECT_EQ(pairing.steady_ns, 200u);
    EXPECT_EQ(pairing.uncertainty_ns, 5u);
}

/** @brief Tests that a device clock holds the device and its pairing as they were given. */
TEST(Small_ClockPairing, ADeviceClockKeepsItsDeviceAndPairing)
{
    const DeviceClock clock{2, ClockPairing{100, 200, 5}};

    EXPECT_EQ(clock.device_index, 2u);
    EXPECT_EQ(clock.pairing.event_ns, 100u);
    EXPECT_EQ(clock.pairing.steady_ns, 200u);
    EXPECT_EQ(clock.pairing.uncertainty_ns, 5u);
}
