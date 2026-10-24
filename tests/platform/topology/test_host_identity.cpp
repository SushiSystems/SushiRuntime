/**************************************************************************/
/* test_host_identity.cpp                                                 */
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
 * @file test_host_identity.cpp
 * @brief Tests the queries that name the machine the library runs on.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/operating_system.hpp>
#include <SushiRuntime/topology/host_identity.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

/** @brief Tests that the host name is stable between two calls and holds no terminator. */
TEST(Small_HostIdentity, HostNameIsStableAndHoldsNoTerminator)
{
    const std::string first = host_name();
    EXPECT_EQ(host_name(), first);
    EXPECT_EQ(first.find('\0'), std::string::npos);
}

/** @brief Tests that the operating system is the one this test was compiled for. */
TEST(Small_HostIdentity, OperatingSystemIsTheOneCompiledFor)
{
#if defined(_WIN32)
    EXPECT_EQ(host_operating_system(), OperatingSystem::Windows);
#elif defined(__linux__)
    EXPECT_EQ(host_operating_system(), OperatingSystem::Linux);
#else
    EXPECT_EQ(host_operating_system(), OperatingSystem::Unknown);
#endif
}
