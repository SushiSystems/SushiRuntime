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
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

namespace
{
    using SushiRuntime::make_sushi;
    using SushiRuntime::sushi_ptr;
    using SushiRuntime::Internal::Graph::Node;

    /** @brief Describes a machine with no core map; see README.md. */
    class SingleNodeTopology final : public SushiRuntime::Internal::Topology::TopologyProvider
    {
        public:
            SushiRuntime::Result<std::vector<SushiRuntime::Internal::Topology::HardwareNode>>
            enumerate(const SushiRuntime::DeviceFilter&) override
            {
                return std::vector<SushiRuntime::Internal::Topology::HardwareNode>{};
            }
    };

    /** @brief Reports one NUMA node at the local distance. */
    class SingleNodeDistance final : public SushiRuntime::Internal::Topology::NUMADistanceModel
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
    std::vector<SushiRuntime::Internal::Device::DeviceDescriptor> default_device()
    {
        SushiRuntime::Internal::Device::DeviceDescriptor descriptor;
        descriptor.device = sycl::device{sycl::default_selector_v};
        descriptor.info.name = "default";
        descriptor.info.backend = descriptor.device.get_backend();
        return {descriptor};
    }

    /** @brief Counts what the scheduler offers and reports; declines an offer unless told. */
    class CountingCoordinator final : public SushiRuntime::Internal::Graph::OffloadCoordinator
    {
        public:
            void bind(SushiRuntime::Internal::Graph::OffloadSink&) override
            {

            }

            void prepare(SushiRuntime::span<const sushi_ptr<Node>>) override
            {

            }

            [[nodiscard]] bool try_offload(Node& node) override
            {
                offers_.fetch_add(1, std::memory_order_relaxed);
                if (!accepting_.load(std::memory_order_relaxed))
                    return false;
                held_.store(&node, std::memory_order_release);
                return true;
            }

            void on_local_completion(Node&) noexcept override
            {
                completions_.fetch_add(1, std::memory_order_relaxed);
            }

            void stop() noexcept override
            {

            }

            /** @brief Makes try_offload() take the next nodes, or decline them again. */
            void set_accepting(bool accepting) noexcept
            {
                accepting_.store(accepting, std::memory_order_relaxed);
            }

            /** @brief Returns the node try_offload() took last, or nullptr. */
            [[nodiscard]] Node* held() const noexcept
            {
                return held_.load(std::memory_order_acquire);
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
            /// @brief Whether try_offload() takes the node.
            std::atomic<bool> accepting_{false};
            /// @brief The node try_offload() took last.
            std::atomic<Node*> held_{nullptr};
    };

    /**
     * @brief Builds a host node that adds one to @p runs when the pool runs it.
     * @param runs Must outlive the node.
     */
    sushi_ptr<Node> make_counting_node(std::atomic<int>& runs)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::Host;
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
        node->mode = Node::ExecutionMode::Host;
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
              scheduler_(SushiRuntime::Internal::Scheduler::WorkerPoolSettings{
                             2, 0, SushiRuntime::PinPolicy::Float},
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
        SushiRuntime::Internal::Topology::TopologyView topology_;
        /// @brief The default device's queues; the host nodes of these tests use none.
        SushiRuntime::Internal::Device::DeviceQueueSet devices_;
        /// @brief Allocator over the device set; no test allocates.
        SushiRuntime::Internal::Memory::USMAllocator memory_;
        /// @brief The scheduler under test, started and armed.
        SushiRuntime::Internal::Scheduler::TaskScheduler scheduler_;
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
    disabled->bindings_ = std::make_shared<SushiRuntime::Internal::Graph::LateBindings>();
    disabled->bindings_->enabled.store(false, std::memory_order_relaxed);
    run(*disabled);
    EXPECT_EQ(coordinator_.completions(), 1u) << "a node disabled for the step was reported";

    EXPECT_EQ(runs.load(), 1);

    const sushi_ptr<Node> thrower = make_throwing_node();
    run(*thrower);
    ASSERT_TRUE(scheduler_.run_failed());
    EXPECT_EQ(coordinator_.completions(), 1u) << "a node whose work threw was reported";
}

/**
 * @brief The scheduler counts the offers a coordinator took, and no offer it declined.
 */
TEST_F(Unit_OffloadSeam, OnlyAcceptedOffersAreCounted)
{
    std::atomic<int> runs{0};
    ASSERT_EQ(scheduler_.offloaded_task_count(), 0u);

    const sushi_ptr<Node> declined = make_offloadable_node(runs);
    run(*declined);
    EXPECT_EQ(coordinator_.offers(), 1u);
    EXPECT_EQ(scheduler_.offloaded_task_count(), 0u) << "a declined offer was counted";
    EXPECT_EQ(runs.load(), 1);

    coordinator_.set_accepting(true);
    const sushi_ptr<Node> accepted = make_offloadable_node(runs);
    run(*accepted);
    coordinator_.set_accepting(false);
    ASSERT_EQ(coordinator_.held(), accepted.get());
    EXPECT_EQ(scheduler_.offloaded_task_count(), 1u);
    EXPECT_EQ(runs.load(), 1) << "the pool ran a node the coordinator took";

    // The coordinator's reference goes back the way a finished remote node returns.
    scheduler_.complete_remote(*accepted);
    EXPECT_EQ(scheduler_.offloaded_task_count(), 1u);
}
