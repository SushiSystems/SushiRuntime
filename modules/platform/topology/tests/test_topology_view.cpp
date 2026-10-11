/**************************************************************************/
/* test_topology_view.cpp                                                 */
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
 * @file test_topology_view.cpp
 * @brief Tests TopologyView against synthetic topologies and the hwloc provider.
 * @author Mustafa Garip
 */

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>

using namespace SushiRuntime::Internal::Topology;
using SushiRuntime::Errc;
using SushiRuntime::Error;
using SushiRuntime::SushiException;

namespace
{
    /**
     * @brief Runs @p build and returns the Error it throws, failing when it throws none.
     */
    template <typename Build>
    Error refusal_of(Build&& build)
    {
        try
        {
            build();
            ADD_FAILURE() << "the constructor accepted its argument";
        }
        catch (const SushiException& refusal)
        {
            return refusal.error();
        }
        return Error{};
    }

    /// @brief Remote cost of the two-node matrix; differs from the model's fallback of 21.
    constexpr int K_REMOTE_COST = 32;

    /**
     * @brief Serves a constant core map and counts how often it is read.
     */
    class FixedTopologyProvider final : public TopologyProvider
    {
        public:
            /**
             * @brief Stores the map the provider reports.
             */
            explicit FixedTopologyProvider(std::vector<int> map) : map_(std::move(map))
            {
            }

            /**
             * @brief Returns no devices; the view never asks for them.
             */
            std::vector<HardwareNode> enumerate(const HardwareFilterConfig&) override
            {
                return {};
            }

            /**
             * @brief Returns the stored map and counts the call.
             */
            std::vector<int> cpu_core_to_numa() const override
            {
                ++read_count_;
                return map_;
            }

            /**
             * @brief Replaces the map the provider reports from now on.
             */
            void set_map(std::vector<int> map)
            {
                map_ = std::move(map);
            }

            /**
             * @brief Returns how many times cpu_core_to_numa() ran.
             */
            int read_count() const noexcept
            {
                return read_count_;
            }

        private:
            /// @brief Map handed to every caller.
            std::vector<int> map_;
            /// @brief Number of cpu_core_to_numa() calls so far.
            mutable int read_count_ = 0;
    };

    /**
     * @brief Builds a two-node model: 10 on the diagonal, K_REMOTE_COST across.
     */
    std::shared_ptr<const NUMADistanceModel> make_two_node_model()
    {
        return std::make_shared<MatrixDistanceModel>(
            std::vector<std::vector<int>>{{10, K_REMOTE_COST},
                                          {K_REMOTE_COST, 10}});
    }
} // namespace

/**
 * @brief Verifies that the view reports the map of a hand-built two-node provider.
 */
TEST(Unit_TopologyView, CoreMapMatchesSyntheticProvider)
{
    const std::vector<int> expected{0, 0, 1, 1};
    FixedTopologyProvider provider(expected);

    TopologyView view(provider, make_two_node_model());

    EXPECT_EQ(view.core_to_numa(), expected);
}

/**
 * @brief Verifies that the view reads the provider once and keeps its own copy of the map.
 */
TEST(Unit_TopologyView, CoreMapIsReadOnceAtConstruction)
{
    const std::vector<int> expected{0, 1, 0, 1};
    FixedTopologyProvider provider(expected);

    TopologyView view(provider, make_two_node_model());
    provider.set_map({1, 1, 1, 1, 1, 1});

    EXPECT_EQ(view.core_to_numa(), expected);
    EXPECT_EQ(view.numa_distance(0, 1), K_REMOTE_COST);
    EXPECT_EQ(provider.read_count(), 1);
}

/**
 * @brief Verifies that a provider without a core map leaves the view's map empty.
 */
TEST(Unit_TopologyView, CoreMapIsEmptyWhenProviderSuppliesNone)
{
    FixedTopologyProvider provider({});

    TopologyView view(provider, make_two_node_model());

    EXPECT_TRUE(view.core_to_numa().empty());
}

/**
 * @brief Verifies that distances equal the matrix entries, diagonal included.
 */
TEST(Unit_TopologyView, DistancesMatchMatrixIncludingDiagonal)
{
    FixedTopologyProvider provider({0, 1});
    auto model = std::make_shared<MatrixDistanceModel>(
        std::vector<std::vector<int>>{{10, 16, 32},
                                      {17, 10, 18},
                                      {33, 19, 10}});

    TopologyView view(provider, model);

    for (int a = 0; a < model->node_count(); ++a)
        for (int b = 0; b < model->node_count(); ++b)
            EXPECT_EQ(view.numa_distance(a, b), model->distance(a, b)) << a << " -> " << b;

    EXPECT_EQ(view.numa_distance(0, 0), 10);
    EXPECT_EQ(view.numa_distance(2, 2), 10);
    EXPECT_EQ(view.numa_distance(0, 2), 32);
    EXPECT_EQ(view.numa_distance(2, 0), 33);
}

/**
 * @brief Verifies that out-of-range ids get the model's answer: 10 when equal, 21 otherwise.
 */
TEST(Unit_TopologyView, OutOfRangeIDsGetTheModelsAnswer)
{
    FixedTopologyProvider provider({0, 1});

    TopologyView view(provider, make_two_node_model());

    EXPECT_EQ(view.numa_distance(5, 5), 10);
    EXPECT_EQ(view.numa_distance(-1, -1), 10);
    EXPECT_EQ(view.numa_distance(0, 5), 21);
    EXPECT_EQ(view.numa_distance(5, 0), 21);
    EXPECT_EQ(view.numa_distance(-1, 0), 21);
    EXPECT_EQ(view.numa_distance(2, 3), 21);
}

/**
 * @brief Verifies that the constructor rejects a null distance model.
 */
TEST(Unit_TopologyView, NullDistanceModelIsRejected)
{
    FixedTopologyProvider provider({0, 1});

    const Error refusal = refusal_of([&] { TopologyView view(provider, nullptr); });

    EXPECT_EQ(refusal.code, Errc::invalid_access);
    EXPECT_NE(refusal.message.find("distance model is null"), std::string::npos)
        << refusal.message;
}

/**
 * @brief Verifies that a remapping over no provider is rejected.
 */
TEST(Unit_TopologyView, NullInnerProviderIsRejected)
{
    const Error refusal = refusal_of([] { RemappingTopologyProvider provider(nullptr, 2); });

    EXPECT_EQ(refusal.code, Errc::invalid_access);
    EXPECT_NE(refusal.message.find("inner provider is null"), std::string::npos)
        << refusal.message;
}

/**
 * @brief Verifies that a ragged matrix is rejected with the row and both sizes.
 */
TEST(Unit_TopologyView, RaggedDistanceMatrixIsRejectedWithItsRow)
{
    const Error refusal =
        refusal_of([] { MatrixDistanceModel model({{10, 21, 21}, {21, 10}, {21, 21, 10}}); });

    EXPECT_EQ(refusal.code, Errc::invalid_access);
    EXPECT_NE(refusal.message.find("row 1 holds 2 entries"), std::string::npos)
        << refusal.message;
    EXPECT_NE(refusal.message.find("has 3 rows"), std::string::npos) << refusal.message;
}

/**
 * @brief Verifies the map of a two-node remapping laid over the real cores.
 */
TEST(Unit_TopologyView, CoreMapMatchesRemappingProvider)
{
    constexpr int FAKE_NODES = 2;
    RemappingTopologyProvider provider(std::make_shared<HwlocTopologyProvider>(), FAKE_NODES);

    TopologyView view(provider, make_two_node_model());

    EXPECT_EQ(view.core_to_numa(), provider.cpu_core_to_numa());
    for (int node : view.core_to_numa())
    {
        EXPECT_GE(node, 0);
        EXPECT_LT(node, FAKE_NODES);
    }
}

/**
 * @brief Verifies one map entry per logical core of the real machine, each a valid node id.
 */
TEST(Unit_TopologyView, HwlocProviderYieldsOneValidNodePerLogicalCore)
{
    HwlocTopologyProvider provider;
    const std::vector<int> reported = provider.cpu_core_to_numa();
    ASSERT_FALSE(reported.empty()) << "hwloc reported no logical cores.";

    TopologyView view(provider, std::make_shared<HwlocDistanceModel>());

    ASSERT_EQ(view.core_to_numa().size(), reported.size());
    EXPECT_EQ(view.core_to_numa(), reported);
    for (int node : view.core_to_numa())
    {
        EXPECT_GE(node, 0);
        EXPECT_EQ(view.numa_distance(node, node), 10);
    }
}
