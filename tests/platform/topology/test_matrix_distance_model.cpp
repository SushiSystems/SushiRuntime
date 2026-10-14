/**************************************************************************/
/* test_matrix_distance_model.cpp                                         */
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
 * @file test_matrix_distance_model.cpp
 * @brief Tests MatrixDistanceModel: the ACPI convention and an asymmetric fabric.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/matrix_distance_model.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace TopologyAbstractionTests
{
    /**
     * @brief Verifies that a hand-built MatrixDistanceModel yields exact, deterministic distances.
     *
     * Creates a synthetic 2-node distance matrix and asserts local distances are 10 while remote distances match the provided configuration.
     */
    TEST(Unit_TopologyAbstractionTest, MatrixDistanceModelObeysACPIConvention)
    {
        MatrixDistanceModel model{{{10, 21},
                                   {21, 10}}};

        EXPECT_EQ(model.node_count(), 2);
        EXPECT_EQ(model.distance(0, 0), 10); // local
        EXPECT_EQ(model.distance(1, 1), 10); // local
        EXPECT_EQ(model.distance(0, 1), 21); // remote
        EXPECT_EQ(model.distance(1, 0), 21); // symmetric remote
    }

    /**
     * @brief Verifies that the MatrixDistanceModel correctly supports asymmetric multi-hop fabric
     *        distances.
     *
     * Constructs a 3-node matrix with non-uniform interconnect costs and verifies correct distance retrievals.
     */
    TEST(Unit_TopologyAbstractionTest, MatrixDistanceModelSupportsAsymmetricFabric)
    {
        // 3-node layout: 0<->1 cheap, 0<->2 expensive (2 hops).
        MatrixDistanceModel model{{{10, 16, 32},
                                   {16, 10, 16},
                                   {32, 16, 10}}};

        EXPECT_EQ(model.distance(0, 1), 16);
        EXPECT_EQ(model.distance(0, 2), 32);
        EXPECT_GT(model.distance(0, 2), model.distance(0, 1));
    }
} // namespace TopologyAbstractionTests
