/**************************************************************************/
/* test_offload_seam.cpp                                                  */
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
 * @file test_offload_seam.cpp
 * @brief Tests what the scheduler offers and reports to an offload coordinator.
 * @see modules/engine/scheduler/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_info.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/i_topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/scheduler/pin_policy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Graph::Node;

    /** @brief Describes a machine with no core map; see README.md. */
    class SingleNodeTopology final : public SushiRuntime::Topology::ITopologyProvider
    {
        public:
            std::vector<SushiRuntime::Topology::HardwareNode>
            enumerate(const SushiRuntime::Topology::HardwareFilterConfig&) override
            {
                return {};
            }
    };

    /** @brief Reports one NUMA node at the local distance. */
    class SingleNodeDistance final : public SushiRuntime::Topology::INumaDistanceModel
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

    /** @brief Returns an inventory of the one device SYCL selects by default. */
    std::vector<SushiRuntime::Device::DeviceDescriptor> default_device()
    {
        SushiRuntime::Device::DeviceDescriptor descriptor;
        descriptor.device = sycl::device{sycl::default_selector_v};
        descriptor.info.name = "default";
        descriptor.info.backend = descriptor.device.get_backend();
        return {descriptor};
    }

    /** @brief Counts what the scheduler offers and reports, and declines every offer. */
    class CountingCoordinator final : public SushiRuntime::Graph::OffloadCoordinator
    {
        public:
            void bind(SushiRuntime::Graph::OffloadSink&) override
            {

            }

            void prepare(SushiRuntime::span<const sushi_ptr<Node>>) override
            {

            }

            [[nodiscard]] bool try_offload(Node&) override
            {
                offers_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            void on_local_completion(Node&) noexcept override
            {
                completions_.fetch_add(1, std::memory_order_relaxed);
            }

            void share(void*, std::size_t) override
            {

            }

            [[nodiscard]] SushiRuntime::Graph::OffloadStatistics
            statistics() const noexcept override
            {
                return {};
            }

            void stop() noexcept override
            {

            }

            /** @brief Returns how many nodes try_offload() was offered. */
            [[nodiscard]] std::size_t offers() const noexcept
            {
                return offers_.load(std::memory_order_relaxed);
            }

            /** @brief Returns how many nodes on_local_completion() was told of. */
            [[nodiscard]] std::size_t completions() const noexcept
            {
                return completions_.load(std::memory_order_relaxed);
            }

        private:
            /// @brief Calls of try_offload().
            std::atomic<std::size_t> offers_{0};
            /// @brief Calls of on_local_completion().
            std::atomic<std::size_t> completions_{0};
    };

    /**
     * @brief Builds a host node that adds one to @p runs when the pool runs it.
     * @param runs Must outlive the node.
     */
    sushi_ptr<Node> make_counting_node(std::atomic<int>& runs)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::HOST;
        node->submits_device_work_ = false;
        node->native_work = [&runs] { runs.fetch_add(1, std::memory_order_relaxed); };
        return node;
    }

    /** @brief Builds a counting node the scheduler offers to the coordinator. */
    sushi_ptr<Node> make_offloadable_node(std::atomic<int>& runs)
    {
        sushi_ptr<Node> node = make_counting_node(runs);
        node->offload().offloadable = true;
        return node;
    }

    /** @brief Builds a host node whose work throws, which fails the run. */
    sushi_ptr<Node> make_throwing_node()
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::HOST;
        node->submits_device_work_ = false;
        node->native_work = [] { throw std::runtime_error("offload seam test failure"); };
        return node;
    }
} // namespace

/** @brief Runs a two-worker scheduler with a counting coordinator installed; see README.md. */
class Unit_OffloadSeam : public ::testing::Test
{
    protected:
        Unit_OffloadSeam()
            : topology_(SingleNodeTopology(), std::make_shared<SingleNodeDistance>()),
              devices_(default_device(), false),
              memory_(devices_.plain_queues()),
              scheduler_(SushiRuntime::Scheduler::WorkerPoolSettings{
                             2, 0, SushiRuntime::Scheduler::PinPolicy::Float},
                         topology_, devices_, memory_)
        {
            scheduler_.set_offload_coordinator(&coordinator_);
            scheduler_.start();
            scheduler_.arm_for_execution();
        }

        ~Unit_OffloadSeam() override
        {
            scheduler_.stop();
        }

        /** @brief Submits @p node and returns once the pool has reaped it. */
        void run(Node& node)
        {
            scheduler_.submit(&node);
            scheduler_.wait_quiescent();
        }

        /// @brief Declared before the scheduler, which keeps its address until stop().
        CountingCoordinator coordinator_;
        /// @brief One NUMA node and no core map.
        SushiRuntime::Topology::TopologyView topology_;
        /// @brief The default device's queues; the host nodes of these tests use none.
        SushiRuntime::Device::DeviceQueueSet devices_;
        /// @brief Allocator over the device set; no test allocates.
        SushiRuntime::Memory::UsmAllocator memory_;
        /// @brief The scheduler under test, started and armed.
        SushiRuntime::Scheduler::TaskScheduler scheduler_;
};

/**
 * @brief A cancelled or failed run offers no node to the coordinator.
 */
TEST_F(Unit_OffloadSeam, AbortedRunOffersNoNode)
{
    std::atomic<int> runs{0};

    // The control proves the coordinator is offered a node when the run is healthy.
    const sushi_ptr<Node> healthy = make_offloadable_node(runs);
    run(*healthy);
    ASSERT_EQ(coordinator_.offers(), 1u);
    ASSERT_EQ(runs.load(), 1);

    const sushi_ptr<Node> cancelled = make_offloadable_node(runs);
    scheduler_.request_cancellation();
    run(*cancelled);
    scheduler_.reset_cancellation();
    EXPECT_EQ(coordinator_.offers(), 1u) << "a cancelled run offered a node";
    EXPECT_EQ(runs.load(), 1) << "a cancelled run executed a node";

    const sushi_ptr<Node> thrower = make_throwing_node();
    run(*thrower);
    ASSERT_TRUE(scheduler_.run_failed());

    const sushi_ptr<Node> after_failure = make_offloadable_node(runs);
    run(*after_failure);
    EXPECT_EQ(coordinator_.offers(), 1u) << "a failed run offered a node";
    EXPECT_EQ(runs.load(), 1) << "a failed run executed a node";
}

/**
 * @brief A node the scheduler releases without running is not reported as completed.
 */
TEST_F(Unit_OffloadSeam, SkippedNodeNotReportedAsCompleted)
{
    std::atomic<int> runs{0};

    // The control proves an executed node is reported, offloadable or not.
    const sushi_ptr<Node> executed = make_counting_node(runs);
    run(*executed);
    ASSERT_EQ(coordinator_.completions(), 1u);
    ASSERT_EQ(runs.load(), 1);

    const sushi_ptr<Node> cancelled = make_counting_node(runs);
    scheduler_.request_cancellation();
    run(*cancelled);
    scheduler_.reset_cancellation();
    EXPECT_EQ(coordinator_.completions(), 1u) << "a node skipped by a cancelled run was reported";

    const sushi_ptr<Node> disabled = make_counting_node(runs);
    disabled->bindings_ = std::make_shared<SushiRuntime::Graph::LateBindings>();
    disabled->bindings_->enabled.store(false, std::memory_order_relaxed);
    run(*disabled);
    EXPECT_EQ(coordinator_.completions(), 1u) << "a node disabled for the step was reported";

    EXPECT_EQ(runs.load(), 1);

    const sushi_ptr<Node> thrower = make_throwing_node();
    run(*thrower);
    ASSERT_TRUE(scheduler_.run_failed());
    EXPECT_EQ(coordinator_.completions(), 1u) << "a node whose work threw was reported";
}
