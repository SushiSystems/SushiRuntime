/**************************************************************************/
/* test_cluster_statistics.cpp                                            */
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
 * @file test_cluster_statistics.cpp
 * @brief Tests ClusterStatistics and WorkerSnapshot: everything starts at zero and empty.
 * @see tests/extension/cluster_load/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/cluster_load/cluster_statistics.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that the statistics of a cluster with no worker are zero. */
TEST(Unit_ClusterStatistics, StartAtZero)
{
    const ClusterStatistics statistics;
    EXPECT_EQ(statistics.alive_workers, 0u);
    EXPECT_EQ(statistics.pending_tasks, 0u);
    EXPECT_TRUE(statistics.workers.empty());
    const WorkerSnapshot worker;
    EXPECT_TRUE(worker.name.empty());
    EXPECT_TRUE(worker.endpoint.empty());
}
