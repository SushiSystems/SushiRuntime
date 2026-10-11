/**************************************************************************/
/* test_node_placement.cpp                                                */
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
 * @file test_node_placement.cpp
 * @brief Tests which device queue the scheduler hands a graph node at dispatch.
 * @see modules/engine/scheduler/tests/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_info.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/scheduler/pin_policy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;

    /** @brief Stands for "handed a queue of no entry", and for a node whose work did not run. */
    constexpr std::size_t K_NO_DEVICE = static_cast<std::size_t>(-1);

    /** @brief Describes a machine with no core map; see README.md. */
    class SingleNodeTopology final : public SushiRuntime::Internal::Topology::ITopologyProvider
    {
        public:
            std::vector<SushiRuntime::Internal::Topology::HardwareNode>
            enumerate(const SushiRuntime::Internal::Topology::HardwareFilterConfig&) override
            {
                return {};
            }
    };

    /** @brief Reports one NUMA node at the local distance. */
    class SingleNodeDistance final : public SushiRuntime::Internal::Topology::INUMADistanceModel
    {
        public:
            int distance(int, int) const override
            {
                return 10;
            }

            int node_count() const override
            {
                return 1;
            }
    };

    /** @brief Returns two entries over the device SYCL selects by default; see README.md. */
    std::vector<SushiRuntime::Internal::Device::DeviceDescriptor> two_entries_over_the_default_device()
    {
        SushiRuntime::Internal::Device::DeviceDescriptor descriptor;
        descriptor.device = sycl::device{sycl::default_selector_v};
        descriptor.info.name = "default";
        descriptor.info.backend = descriptor.device.get_backend();
        descriptor.info.numa_node = 0;
        return {descriptor, descriptor};
    }

    /**
     * @brief Builds a host node that submits device work and records the queue it is handed.
     * @param handed Receives a copy of the queue; must outlive the node.
     */
    sushi_ptr<Node> make_recording_node(std::optional<sycl::queue>& handed)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::HOST;
        node->host_work = [&handed](sycl::queue& queue, const std::vector<sycl::event>&)
        {
            handed = queue;
            return sycl::event{};
        };
        return node;
    }
} // namespace

/** @brief Runs a two-worker scheduler over a two-entry device set; see README.md. */
class Unit_NodePlacement : public ::testing::Test
{
    protected:
        Unit_NodePlacement()
            : topology_(SingleNodeTopology(), std::make_shared<SingleNodeDistance>()),
              devices_(two_entries_over_the_default_device(), false),
              memory_(devices_.plain_queues()),
              scheduler_(SushiRuntime::Internal::Scheduler::WorkerPoolSettings{
                             2, 0, SushiRuntime::Internal::Scheduler::PinPolicy::Float},
                         topology_, devices_, memory_)
        {
            scheduler_.start();
            scheduler_.arm_for_execution();
        }

        ~Unit_NodePlacement() override
        {
            scheduler_.stop();
        }

        /**
         * @brief Runs @p node once and returns the entry whose queue it was handed.
         * @param handed The slot the node's work records into.
         * @return The entry's index, or K_NO_DEVICE when the node's work did not run.
         */
        std::size_t dispatched_device(Node& node, std::optional<sycl::queue>& handed)
        {
            handed.reset();
            scheduler_.submit(&node);
            scheduler_.wait_quiescent();

            if (!handed)
                return K_NO_DEVICE;
            for (std::size_t index = 0; index < devices_.count(); ++index)
                if (devices_.queue(index) == *handed)
                    return index;
            return K_NO_DEVICE;
        }

        /// @brief One NUMA node and no core map.
        SushiRuntime::Internal::Topology::TopologyView topology_;
        /// @brief Two entries on NUMA node 0, each with its own queue on one SYCL device.
        SushiRuntime::Internal::Device::DeviceQueueSet devices_;
        /// @brief Allocator over the two entries; a test records ownership with adopt().
        SushiRuntime::Internal::Memory::USMAllocator memory_;
        /// @brief The scheduler under test, started and armed.
        SushiRuntime::Internal::Scheduler::TaskScheduler scheduler_;
};

/**
 * @brief Two region-less nodes run on device 0 and a node placed on device 1 runs there.
 */
TEST_F(Unit_NodePlacement, RegionlessNodesRunOnDeviceZeroAndAPlacedNodeOnItsDevice)
{
    // The control proves the two entries can be told apart by their queues.
    ASSERT_EQ(devices_.count(), 2u);
    ASSERT_FALSE(devices_.queue(0) == devices_.queue(1));

    std::optional<sycl::queue> handed;

    const sushi_ptr<Node> first = make_recording_node(handed);
    EXPECT_EQ(dispatched_device(*first, handed), 0u);
    EXPECT_EQ(first->cached_queue_index_, 0);

    const sushi_ptr<Node> second = make_recording_node(handed);
    EXPECT_EQ(dispatched_device(*second, handed), 0u);
    EXPECT_EQ(second->cached_queue_index_, 0);

    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->place_on(1);
    EXPECT_EQ(dispatched_device(*placed, handed), 1u);
    EXPECT_EQ(placed->cached_queue_index_, 1);
}

/**
 * @brief A node is handed the same device on every step.
 */
TEST_F(Unit_NodePlacement, ANodeKeepsItsDeviceOnEveryStep)
{
    std::optional<sycl::queue> handed;

    const sushi_ptr<Node> regionless = make_recording_node(handed);
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->place_on(1);

    for (int step = 0; step < 4; ++step)
    {
        EXPECT_EQ(dispatched_device(*regionless, handed), 0u) << "step " << step;
        EXPECT_EQ(dispatched_device(*placed, handed), 1u) << "step " << step;
    }
}

/**
 * @brief A node that names an owned region runs on the owner, whatever placement it states.
 */
TEST_F(Unit_NodePlacement, ARegionOwnerOutranksTheStatedPlacement)
{
    int cell = 0;
    memory_.adopt(&cell, 1, sizeof(cell));

    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> node = make_recording_node(handed);
    node->io_writes_.emplace_back(&cell);
    node->place_on(0);

    EXPECT_EQ(dispatched_device(*node, handed), 1u);

    memory_.forget(&cell);
}

/**
 * @brief A node whose region no device owns runs on the placement it states, else on device 0.
 */
TEST_F(Unit_NodePlacement, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;

    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->io_reads_.emplace_back(&cell);
    placed->place_on(1);
    EXPECT_EQ(dispatched_device(*placed, handed), 1u);

    const sushi_ptr<Node> unplaced = make_recording_node(handed);
    unplaced->io_reads_.emplace_back(&cell);
    EXPECT_EQ(dispatched_device(*unplaced, handed), 0u);
}

/**
 * @brief A placement past the device set fails the run with the index and the device count.
 */
TEST_F(Unit_NodePlacement, APlacementPastTheDeviceSetFailsTheRun)
{
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> node = make_recording_node(handed);
    node->metadata.name = "misplaced";
    node->place_on(2);

    EXPECT_EQ(dispatched_device(*node, handed), K_NO_DEVICE);
    ASSERT_TRUE(scheduler_.run_failed());

    const std::exception_ptr error = scheduler_.run_error();
    ASSERT_TRUE(error != nullptr);

    std::optional<SushiRuntime::Errc> code;
    std::string message;
    try
    {
        std::rethrow_exception(error);
    }
    catch (const SushiRuntime::SushiException& failure)
    {
        code = failure.error().code;
        message = failure.what();
    }
    catch (const std::exception& failure)
    {
        message = failure.what();
    }

    ASSERT_TRUE(code.has_value()) << "the run failed with another exception type: " << message;
    EXPECT_EQ(*code, SushiRuntime::Errc::invalid_graph);
    EXPECT_NE(message.find("node 'misplaced'"), std::string::npos) << message;
    EXPECT_NE(message.find("device 2"), std::string::npos) << message;
    EXPECT_NE(message.find("2 device(s)"), std::string::npos) << message;
}
