/**************************************************************************/
/* test_task_scheduler.cpp                                                */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_task_scheduler.cpp
 * @brief Tests the TaskScheduler: device placement, the offload seam, exact execution, steal order and the log lines of its pool.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>
#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/in_memory_offload_coordinator.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>
#include <SushiRuntime/topology/in_memory_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/topology_view.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;
using SushiRuntime::Internal::Graph::Node;
using SushiRuntime::Internal::Scheduler::TaskScheduler;
using SushiRuntime::Internal::Scheduler::WorkerPoolSettings;

namespace
{
    /** @brief Stands for "handed a queue of no entry", and for a node whose work did not run. */
    constexpr std::size_t K_NO_DEVICE = static_cast<std::size_t>(-1);

    /** @brief Builds an inventory of @p count entries over the device SYCL selects by default, all on NUMA node 0. */
    std::vector<Internal::Device::DeviceDescriptor> entries_over_the_default_device(std::size_t count)
    {
        std::vector<Internal::Device::DeviceDescriptor> entries;
        for (std::size_t index = 0; index < count; ++index)
        {
            Internal::Device::DeviceDescriptor descriptor;
            descriptor.device = sycl::device{sycl::default_selector_v};
            descriptor.info.name = "default";
            descriptor.info.backend = descriptor.device.get_backend();
            descriptor.info.numa_node = 0;
            entries.push_back(descriptor);
        }
        return entries;
    }

    /** @brief Builds a distance matrix of @p nodes NUMA nodes: 10 to itself, 21 to another. */
    std::vector<std::vector<int>> distances_between(int nodes)
    {
        std::vector<std::vector<int>> matrix(static_cast<std::size_t>(nodes),
                                             std::vector<int>(static_cast<std::size_t>(nodes), 21));
        for (std::size_t node = 0; node < matrix.size(); ++node)
            matrix[node][node] = 10;
        return matrix;
    }

    /** @brief Builds a core map that gives core i the NUMA node i modulo @p nodes, for @p cores cores. */
    std::vector<int> cores_alternating_over(std::size_t cores, int nodes)
    {
        std::vector<int> map;
        for (std::size_t core = 0; core < cores; ++core)
            map.push_back(static_cast<int>(core) % nodes);
        return map;
    }

    /**
     * @brief Owns what a scheduler is built on: a topology, a device set and an allocator.
     *
     * The device set makes a queue only at its first use, so building the parts touches no device.
     */
    class Hardware
    {
        public:
            /**
             * @brief Builds the parts.
             * @param entries Device entries of the set; none for a scheduler that only constructs.
             * @param core_to_numa NUMA node of each logical core; empty for a machine with no core map.
             * @param nodes NUMA nodes the distance model knows.
             */
            Hardware(std::vector<Internal::Device::DeviceDescriptor> entries, std::vector<int> core_to_numa,
                     int nodes)
                : topology_(Internal::Topology::InMemoryTopologyProvider({}, std::move(core_to_numa)),
                            std::make_shared<Internal::Topology::MatrixDistanceModel>(distances_between(nodes))),
                  devices_(std::move(entries), false), memory_(devices_.plain_queues())
            {
            }

            /** @brief Builds a scheduler of @p settings over the parts, without starting it. */
            std::unique_ptr<TaskScheduler> make(const WorkerPoolSettings& settings)
            {
                return std::make_unique<TaskScheduler>(settings, topology_, devices_, memory_);
            }

            /** @brief Returns the device set. */
            Internal::Device::DeviceQueueSet& devices() noexcept { return devices_; }

            /** @brief Returns the allocator over the device set. */
            Internal::Memory::USMAllocator& memory() noexcept { return memory_; }

        private:
            /// @brief The topology the scheduler reads its core map and distances from.
            Internal::Topology::TopologyView topology_;
            /// @brief The entries a scheduler dispatches to.
            Internal::Device::DeviceQueueSet devices_;
            /// @brief Allocator over the entries; a test records ownership with adopt().
            Internal::Memory::USMAllocator memory_;
    };

    /** @brief Returns the one hardware of the program with @p count entries, built on the first call. */
    Hardware& shared_hardware(std::size_t count)
    {
        static Hardware one{entries_over_the_default_device(1), {}, 1};
        static Hardware two{entries_over_the_default_device(2), {}, 1};
        return count == 1 ? one : two;
    }

    /** @brief Builds hardware with no device entry and the given core map, for a scheduler that only constructs. */
    std::unique_ptr<Hardware> hardware_without_devices(std::vector<int> core_to_numa, int nodes)
    {
        return std::make_unique<Hardware>(std::vector<Internal::Device::DeviceDescriptor>{},
                                          std::move(core_to_numa), nodes);
    }

    /** @brief Builds a host node that submits device work and records the queue it is handed.
     *  @param handed Receives a copy of the queue; must outlive the node. */
    sushi_ptr<Node> make_recording_node(std::optional<sycl::queue>& handed)
    {
        sushi_ptr<Node> node = make_sushi<Node>();
        node->mode = Node::ExecutionMode::Host;
        node->host_work = [&handed](sycl::queue& queue, const std::vector<sycl::event>&)
        {
            handed = queue;
            return sycl::event{};
        };
        return node;
    }

    /** @brief Builds a host node that adds one to @p runs when the pool runs it.
     *  @param runs Must outlive the node. */
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

    /** @brief Builds @p count counting nodes that add to @p ran. */
    std::vector<sushi_ptr<Node>> counting_nodes(std::atomic<int>& ran, int count)
    {
        std::vector<sushi_ptr<Node>> nodes;
        for (int index = 0; index < count; ++index)
            nodes.push_back(make_counting_node(ran));
        return nodes;
    }

    /** @brief Submits every node of @p nodes to @p scheduler. */
    void submit_all(TaskScheduler& scheduler, const std::vector<sushi_ptr<Node>>& nodes)
    {
        for (const sushi_ptr<Node>& node : nodes)
            scheduler.submit(node.get());
    }

    /** @brief Submits @p count counter tasks that add to @p executed. */
    void submit_counters(TaskScheduler& scheduler, std::atomic<size_t>& executed, std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index)
            scheduler.submit(new SushiRuntime::Test::AtomicCounterTask(executed));
    }

    /** @brief Counts the captured records of @p level and @p module whose message holds @p text. */
    std::size_t count_lines(const TestRunner::LogCapture& capture, SushiLog::LogLevel level,
                            const std::string& module, const std::string& text)
    {
        std::size_t lines = 0;
        for (const TestRunner::CapturedRecord& record : capture.records())
        {
            if (record.level == level && record.module == module && record.message.find(text) != std::string::npos)
                ++lines;
        }
        return lines;
    }

    /** @brief Returns the steal victims of every worker of @p scheduler that violate locality under @p nodes nodes. */
    std::vector<std::string> locality_violations(const TaskScheduler& scheduler, const std::vector<int>& core_to_numa)
    {
        std::vector<std::string> violations;
        for (int worker = 0; worker < static_cast<int>(scheduler.worker_count()); ++worker)
        {
            const int my_node = core_to_numa[static_cast<std::size_t>(worker)];
            bool seen_remote = false;
            for (const int victim : scheduler.steal_victim_order(worker))
            {
                const bool is_local = core_to_numa[static_cast<std::size_t>(victim)] == my_node;
                if (!is_local)
                    seen_remote = true;
                else if (seen_remote)
                    violations.push_back("worker " + std::to_string(worker) + ": local victim " +
                                         std::to_string(victim) + " follows a remote victim");
            }
        }
        return violations;
    }

    /** @brief Returns the sorted steal victims of each worker of @p scheduler. */
    std::vector<std::vector<int>> sorted_victims_of_every_worker(const TaskScheduler& scheduler)
    {
        std::vector<std::vector<int>> all;
        for (int worker = 0; worker < static_cast<int>(scheduler.worker_count()); ++worker)
        {
            std::vector<int> victims = scheduler.steal_victim_order(worker);
            std::sort(victims.begin(), victims.end());
            all.push_back(victims);
        }
        return all;
    }
} // namespace

/** @brief Runs a two-worker scheduler over a two-entry device set; see README.md. */
class Medium_NodePlacement : public TestSupport::RuntimeTest
{
    protected:
        Medium_NodePlacement()
            : hardware_(shared_hardware(2)),
              scheduler_(hardware_.make(WorkerPoolSettings{2, 0, PinPolicy::Float}))
        {
            scheduler_->start();
            scheduler_->arm_for_execution();
        }

        ~Medium_NodePlacement() override
        {
            scheduler_->stop();
        }

        /**
         * @brief Runs @p node once and returns the entry whose queue it was handed.
         * @param handed The slot the node's work records into.
         * @return The entry's index, or K_NO_DEVICE when the node's work did not run.
         */
        std::size_t dispatched_device(Node& node, std::optional<sycl::queue>& handed)
        {
            handed.reset();
            scheduler_->submit(&node);
            scheduler_->wait_quiescent();

            if (!handed)
                return K_NO_DEVICE;
            for (std::size_t index = 0; index < hardware_.devices().count(); ++index)
                if (hardware_.devices().queue(index) == *handed)
                    return index;
            return K_NO_DEVICE;
        }

        /** @brief Runs @p node @p steps times and returns the entry it was handed on each step. */
        std::vector<std::size_t> dispatched_devices(Node& node, std::optional<sycl::queue>& handed, int steps)
        {
            std::vector<std::size_t> entries;
            for (int step = 0; step < steps; ++step)
                entries.push_back(dispatched_device(node, handed));
            return entries;
        }

        /// @brief Two entries on NUMA node 0, each with its own queue on one SYCL device.
        Hardware& hardware_;
        /// @brief The scheduler under test, started and armed.
        std::unique_ptr<TaskScheduler> scheduler_;
};

/** @brief Tests that two nodes with no region and no placement run on entry 0. */
TEST_F(Medium_NodePlacement, RegionlessNodesRunOnDeviceZero)
{
    // The control proves the two entries can be told apart by their queues.
    ASSERT_EQ(hardware_.devices().count(), 2u);
    ASSERT_FALSE(hardware_.devices().queue(0) == hardware_.devices().queue(1));
    std::optional<sycl::queue> handed;

    const sushi_ptr<Node> first = make_recording_node(handed);
    EXPECT_EQ(dispatched_device(*first, handed), 0u);
    EXPECT_EQ(first->cached_queue_index_, 0);

    const sushi_ptr<Node> second = make_recording_node(handed);
    EXPECT_EQ(dispatched_device(*second, handed), 0u);
    EXPECT_EQ(second->cached_queue_index_, 0);
}

/** @brief Tests that a node placed on an entry runs on that entry. */
TEST_F(Medium_NodePlacement, APlacedNodeRunsOnItsDevice)
{
    ASSERT_FALSE(hardware_.devices().queue(0) == hardware_.devices().queue(1));
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->place_on(1);

    EXPECT_EQ(dispatched_device(*placed, handed), 1u);
    EXPECT_EQ(placed->cached_queue_index_, 1);
}

/** @brief Tests that a node keeps the entry it was first dispatched to on every later step. */
TEST_F(Medium_NodePlacement, ANodeKeepsItsDeviceOnEveryStep)
{
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> regionless = make_recording_node(handed);
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->place_on(1);

    EXPECT_EQ(dispatched_devices(*regionless, handed, 4), (std::vector<std::size_t>{0, 0, 0, 0}));
    EXPECT_EQ(dispatched_devices(*placed, handed, 4), (std::vector<std::size_t>{1, 1, 1, 1}));
}

/** @brief Tests that a node over an owned region, stating nothing, runs on the owner. */
TEST_F(Medium_NodePlacement, ANodeOverAnOwnedRegionRunsOnTheOwner)
{
    int cell = 0;
    hardware_.memory().adopt(&cell, 1, sizeof(cell), std::nullopt);
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> unplaced = make_recording_node(handed);
    unplaced->io_writes_.emplace_back(&cell);

    EXPECT_EQ(dispatched_device(*unplaced, handed), 1u);

    hardware_.memory().forget(&cell);
}

/** @brief Tests that a stated placement in the owner's context outranks the owner. */
TEST_F(Medium_NodePlacement, AStatedPlacementInTheOwnersContextOutranksTheOwner)
{
    int cell = 0;
    hardware_.memory().adopt(&cell, 1, sizeof(cell), std::nullopt);
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->io_writes_.emplace_back(&cell);
    placed->place_on(0);

    EXPECT_EQ(dispatched_device(*placed, handed), 0u);

    hardware_.memory().forget(&cell);
}

/** @brief Tests that a node whose region no device owns runs on the placement it states. */
TEST_F(Medium_NodePlacement, AnUnownedRegionLeavesTheStatedPlacementInForce)
{
    int cell = 0;
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> placed = make_recording_node(handed);
    placed->io_reads_.emplace_back(&cell);
    placed->place_on(1);

    EXPECT_EQ(dispatched_device(*placed, handed), 1u);
}

/** @brief Tests that a node whose region no device owns and that states nothing runs on entry 0. */
TEST_F(Medium_NodePlacement, AnUnownedRegionWithNothingStatedRunsOnDeviceZero)
{
    int cell = 0;
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> unplaced = make_recording_node(handed);
    unplaced->io_reads_.emplace_back(&cell);

    EXPECT_EQ(dispatched_device(*unplaced, handed), 0u);
}

/** @brief Tests that a placement past the device set fails the run, naming the node, the index and the device count. */
TEST_F(Medium_NodePlacement, APlacementPastTheDeviceSetFailsTheRun)
{
    std::optional<sycl::queue> handed;
    const sushi_ptr<Node> node = make_recording_node(handed);
    node->metadata.name = "misplaced";
    node->place_on(2);

    EXPECT_EQ(dispatched_device(*node, handed), K_NO_DEVICE);
    ASSERT_TRUE(scheduler_->run_failed());

    const std::optional<SushiRuntime::Error> error = scheduler_->run_error();
    ASSERT_TRUE(error.has_value());
    const std::string& message = error->message;
    EXPECT_EQ(error->code, SushiRuntime::ErrorCode::InvalidGraph) << message;
    EXPECT_NE(message.find("node 'misplaced'"), std::string::npos) << message;
    EXPECT_NE(message.find("device 2"), std::string::npos) << message;
    EXPECT_NE(message.find("2 device(s)"), std::string::npos) << message;
}

/** @brief Runs a two-worker scheduler with an in-memory coordinator installed; see README.md. */
class Medium_OffloadSeam : public TestSupport::RuntimeTest
{
    protected:
        Medium_OffloadSeam()
            : scheduler_(shared_hardware(1).make(WorkerPoolSettings{2, 0, PinPolicy::Float}))
        {
            scheduler_->set_offload_coordinator(&coordinator_);
            scheduler_->start();
            scheduler_->arm_for_execution();
        }

        ~Medium_OffloadSeam() override
        {
            scheduler_->stop();
        }

        /** @brief Submits @p node and returns once the pool has reaped it. */
        void run(Node& node)
        {
            scheduler_->submit(&node);
            scheduler_->wait_quiescent();
        }

        /** @brief Runs one offloadable node on a healthy run, which the coordinator is offered. */
        void run_a_healthy_offer(std::atomic<int>& runs)
        {
            const sushi_ptr<Node> healthy = make_offloadable_node(runs);
            run(*healthy);
        }

        /** @brief Runs one counting node on a healthy run, which the coordinator is told of. */
        void run_an_executed_node(std::atomic<int>& runs)
        {
            const sushi_ptr<Node> executed = make_counting_node(runs);
            run(*executed);
        }

        /// @brief Declared before the scheduler, which keeps its address until stop().
        Internal::Graph::InMemoryOffloadCoordinator coordinator_;
        /// @brief The scheduler under test, started and armed; the host nodes of these tests use no device.
        std::unique_ptr<TaskScheduler> scheduler_;
};

/** @brief Tests that a healthy run offers an offloadable node to the coordinator. */
TEST_F(Medium_OffloadSeam, AHealthyRunOffersAnOffloadableNode)
{
    std::atomic<int> runs{0};

    run_a_healthy_offer(runs);

    EXPECT_EQ(coordinator_.offered().size(), 1u);
    EXPECT_EQ(runs.load(), 1);
}

/** @brief Tests that a cancelled run offers no node to the coordinator. */
TEST_F(Medium_OffloadSeam, ACancelledRunOffersNoNode)
{
    std::atomic<int> runs{0};
    run_a_healthy_offer(runs);
    ASSERT_EQ(coordinator_.offered().size(), 1u) << "the control: a healthy run offers";
    const sushi_ptr<Node> cancelled = make_offloadable_node(runs);
    scheduler_->request_cancellation();

    run(*cancelled);
    scheduler_->reset_cancellation();

    EXPECT_EQ(coordinator_.offered().size(), 1u) << "a cancelled run offered a node";
    EXPECT_EQ(runs.load(), 1) << "a cancelled run executed a node";
}

/** @brief Tests that a failed run offers no node to the coordinator. */
TEST_F(Medium_OffloadSeam, AFailedRunOffersNoNode)
{
    std::atomic<int> runs{0};
    run_a_healthy_offer(runs);
    ASSERT_EQ(coordinator_.offered().size(), 1u) << "the control: a healthy run offers";
    const sushi_ptr<Node> thrower = make_throwing_node();
    run(*thrower);
    ASSERT_TRUE(scheduler_->run_failed());
    const sushi_ptr<Node> after_failure = make_offloadable_node(runs);

    run(*after_failure);

    EXPECT_EQ(coordinator_.offered().size(), 1u) << "a failed run offered a node";
    EXPECT_EQ(runs.load(), 1) << "a failed run executed a node";
}

/** @brief Tests that a node a cancelled run skipped is not reported as completed. */
TEST_F(Medium_OffloadSeam, ANodeSkippedByACancelledRunIsNotReportedAsCompleted)
{
    std::atomic<int> runs{0};
    run_an_executed_node(runs);
    ASSERT_EQ(coordinator_.local_completions().size(), 1u) << "the control: an executed node is reported";
    const sushi_ptr<Node> cancelled = make_counting_node(runs);
    scheduler_->request_cancellation();

    run(*cancelled);
    scheduler_->reset_cancellation();

    EXPECT_EQ(coordinator_.local_completions().size(), 1u) << "a node skipped by a cancelled run was reported";
    EXPECT_EQ(runs.load(), 1);
}

/** @brief Tests that a node disabled for the step is not reported as completed. */
TEST_F(Medium_OffloadSeam, ADisabledNodeIsNotReportedAsCompleted)
{
    std::atomic<int> runs{0};
    run_an_executed_node(runs);
    ASSERT_EQ(coordinator_.local_completions().size(), 1u) << "the control: an executed node is reported";
    const sushi_ptr<Node> disabled = make_counting_node(runs);
    disabled->bindings_ = std::make_shared<SushiRuntime::Internal::Graph::LateBindings>();
    disabled->bindings_->enabled.store(false, std::memory_order_relaxed);

    run(*disabled);

    EXPECT_EQ(coordinator_.local_completions().size(), 1u) << "a node disabled for the step was reported";
    EXPECT_EQ(runs.load(), 1);
}

/** @brief Tests that a node whose work threw is not reported as completed. */
TEST_F(Medium_OffloadSeam, ANodeWhoseWorkThrewIsNotReportedAsCompleted)
{
    std::atomic<int> runs{0};
    run_an_executed_node(runs);
    ASSERT_EQ(coordinator_.local_completions().size(), 1u) << "the control: an executed node is reported";
    const sushi_ptr<Node> thrower = make_throwing_node();

    run(*thrower);

    ASSERT_TRUE(scheduler_->run_failed());
    EXPECT_EQ(coordinator_.local_completions().size(), 1u) << "a node whose work threw was reported";
}

/** @brief Tests that an offer the coordinator declined is not counted as offloaded. */
TEST_F(Medium_OffloadSeam, ADeclinedOfferIsNotCounted)
{
    std::atomic<int> runs{0};
    ASSERT_EQ(scheduler_->offloaded_task_count(), 0u);
    const sushi_ptr<Node> declined = make_offloadable_node(runs);

    run(*declined);

    EXPECT_EQ(coordinator_.offered().size(), 1u);
    EXPECT_EQ(scheduler_->offloaded_task_count(), 0u) << "a declined offer was counted";
    EXPECT_EQ(runs.load(), 1);
}

/** @brief Tests that an offer the coordinator took is counted as offloaded and not run by the pool. */
TEST_F(Medium_OffloadSeam, AnAcceptedOfferIsCounted)
{
    std::atomic<int> runs{0};
    coordinator_.set_accepting(true);
    const sushi_ptr<Node> accepted = make_offloadable_node(runs);

    run(*accepted);

    ASSERT_EQ(coordinator_.accepted().size(), 1u);
    EXPECT_EQ(coordinator_.accepted()[0], accepted.get());
    EXPECT_EQ(scheduler_->offloaded_task_count(), 1u);
    EXPECT_EQ(runs.load(), 0) << "the pool ran a node the coordinator took";
    // The coordinator's reference goes back the way a finished remote node returns.
    scheduler_->complete_remote(*accepted);
}

/** @brief Tests that a node the coordinator finished remotely stays counted as offloaded. */
TEST_F(Medium_OffloadSeam, ACompletedRemoteNodeStaysCounted)
{
    std::atomic<int> runs{0};
    coordinator_.set_accepting(true);
    const sushi_ptr<Node> accepted = make_offloadable_node(runs);
    run(*accepted);

    scheduler_->complete_remote(*accepted);

    EXPECT_EQ(scheduler_->offloaded_task_count(), 1u);
}

/** @brief Tests that the scheduler runs exactly the tasks submitted. */
TEST(Medium_SchedulerIntegration, ExecutesExactlySubmitted)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    Internal::Execution::RuntimeContext& ctx = *ctx_owner;
    TaskScheduler scheduler = SushiRuntime::Test::make_scheduler(ctx);
    std::atomic<size_t> executed{0};
    constexpr size_t TOTAL = 2000;
    scheduler.start();
    scheduler.arm_for_execution();

    submit_counters(scheduler, executed, TOTAL);
    scheduler.wait_quiescent();
    scheduler.stop();

    EXPECT_EQ(executed.load(), TOTAL);
}

/** @brief Tests that stop right after submit drains every submitted task, without deadlock. */
TEST(Medium_TaskScheduler, AbruptStopDrains)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    Internal::Execution::RuntimeContext& ctx = *ctx_owner;
    TaskScheduler scheduler = SushiRuntime::Test::make_scheduler(ctx);
    std::atomic<size_t> executed{0};
    constexpr size_t TOTAL = 1000;
    scheduler.start();
    scheduler.arm_for_execution();
    submit_counters(scheduler, executed, TOTAL);

    scheduler.stop();

    EXPECT_EQ(executed.load(), TOTAL) << "stop() dropped a submitted task";
}

/** @brief Tests that a worker count of 0 asks for one worker per logical core. */
TEST(Medium_TaskScheduler, AWorkerCountOfZeroAsksForOneWorkerPerLogicalCore)
{
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);
    const std::size_t cores = std::max<std::size_t>(std::thread::hardware_concurrency(), 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{0, 0, PinPolicy::Float});

    EXPECT_EQ(scheduler->worker_count(), std::min(cores, K_MAX_WORKER_THREADS));
}

/** @brief Tests that, on a topology split over two NUMA nodes, same-node steal victims precede remote ones. */
TEST(Small_NumaStealTest, LocalVictimsPrecedeRemote)
{
    constexpr int NODES = 2;
    const std::vector<int> core_to_numa = cores_alternating_over(K_MAX_WORKER_THREADS, NODES);
    const std::unique_ptr<Hardware> hardware = hardware_without_devices(core_to_numa, NODES);

    // Construction only: the workers are not started.
    const auto scheduler = hardware->make(WorkerPoolSettings{0, 0, PinPolicy::Pin});

    EXPECT_TRUE(locality_violations(*scheduler, core_to_numa).empty());
}

/** @brief Tests that a flat topology gives each worker every other worker as a steal victim, once. */
TEST(Small_NumaStealTest, FlatTopologyKeepsAllVictims)
{
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{4, 0, PinPolicy::Float});

    const std::vector<std::vector<int>> expected{{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};
    EXPECT_EQ(sorted_victims_of_every_worker(*scheduler), expected);
}

/** @brief Tests that a request past the largest pool is cut to it. */
TEST(Small_TaskScheduler, ARequestPastTheLargestPoolIsCutToIt)
{
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{K_MAX_WORKER_THREADS + 5, 0, PinPolicy::Float});

    EXPECT_EQ(scheduler->worker_count(), K_MAX_WORKER_THREADS);
}

/** @brief Tests that a cut request is logged at Warn, naming the requested count and the maximum. */
TEST(Small_TaskScheduler, ACutRequestIsLoggedAtWarnNamingBothCounts)
{
    TestRunner::LogCapture capture;
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{K_MAX_WORKER_THREADS + 5, 0, PinPolicy::Float});

    const auto records = capture.records();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, SushiLog::LogLevel::Warn);
    EXPECT_EQ(records[0].module, "scheduler");
    EXPECT_NE(records[0].message.find(std::to_string(K_MAX_WORKER_THREADS + 5)), std::string::npos)
        << "the line names the requested count: " << records[0].message;
    EXPECT_NE(records[0].message.find(std::to_string(K_MAX_WORKER_THREADS)), std::string::npos)
        << "and the maximum: " << records[0].message;
}

/** @brief Tests that a request inside the limits logs no warning about the count. */
TEST(Small_TaskScheduler, AWorkerCountInsideTheLimitsLogsNoWarning)
{
    TestRunner::LogCapture capture;
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{2, 0, PinPolicy::Float});

    EXPECT_EQ(scheduler->worker_count(), 2u);
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 0u);
}

/** @brief Tests that a pinned worker whose core the topology does not list is mapped to NUMA node 0, and says so. */
TEST(Small_TaskScheduler, APinnedWorkerWhoseCoreHasNoNumaNodeIsLogged)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{2, 0, PinPolicy::Pin});

    // The pool clamps a pinned range on a machine with fewer cores, so the lines follow the workers it kept.
    EXPECT_EQ(count_lines(capture, SushiLog::LogLevel::Debug, "scheduler", "NUMA node 0"), scheduler->worker_count())
        << "one line per pinned worker the core map does not cover";
}

/** @brief Tests that a floating pool has no core to map and logs no such line. */
TEST(Small_TaskScheduler, AFloatingPoolLogsNoCoreMapLine)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Debug);
    const std::unique_ptr<Hardware> hardware = hardware_without_devices({}, 1);

    const auto scheduler = hardware->make(WorkerPoolSettings{2, 0, PinPolicy::Float});

    ASSERT_TRUE(capture.has_message(SushiLog::LogLevel::Info, "unpinned workers")) << "the sink saw the constructor";
    EXPECT_EQ(count_lines(capture, SushiLog::LogLevel::Debug, "scheduler", "NUMA node 0"), 0u);
}

/** @brief Tests that a cancelled run logs one INFO that names the number of tasks it skipped. */
TEST(Medium_TaskSchedulerCancel, LogsTheTasksASkippedRunLeftUnrun)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);
    const auto scheduler = shared_hardware(1).make(WorkerPoolSettings{2, 0, PinPolicy::Float});
    scheduler->start();
    scheduler->arm_for_execution();
    scheduler->request_cancellation();
    std::atomic<int> ran{0};
    const std::vector<sushi_ptr<Node>> nodes = counting_nodes(ran, 5);

    submit_all(*scheduler, nodes);
    scheduler->wait_quiescent();
    scheduler->disarm();
    scheduler->stop();

    EXPECT_EQ(ran.load(), 0) << "a cancelled run executes no task";
    EXPECT_EQ(count_lines(capture, SushiLog::LogLevel::Info, "scheduler", "5 tasks were skipped"), 1u)
        << "exactly one line names how many tasks the cancel left unrun";
}

/** @brief Tests that a run nobody cancelled logs no skip line. */
TEST(Medium_TaskSchedulerCancel, ARunNobodyCancelledLogsNoSkipLine)
{
    TestRunner::LogCapture capture(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);
    const auto scheduler = shared_hardware(1).make(WorkerPoolSettings{2, 0, PinPolicy::Float});
    scheduler->start();
    scheduler->arm_for_execution();
    std::atomic<int> ran{0};
    const std::vector<sushi_ptr<Node>> nodes = counting_nodes(ran, 1);

    submit_all(*scheduler, nodes);
    scheduler->wait_quiescent();
    scheduler->disarm();
    scheduler->stop();

    EXPECT_EQ(ran.load(), 1);
    ASSERT_TRUE(capture.has_message(SushiLog::LogLevel::Info, "unpinned workers")) << "the sink saw the pool";
    EXPECT_EQ(count_lines(capture, SushiLog::LogLevel::Info, "scheduler", "were skipped"), 0u);
}

/** @brief Tests the scheduler for priority bypass. */
namespace SchedulerIntegrationTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using SushiRuntime::Test::PriorityTrackingTask;

    /**
     * @brief Tests that a late high-priority task bypasses lower priority tasks.
     *
     * Verifies that the scheduler honors task priorities and executes the high priority task among the first.
     */
    TEST(Medium_SchedulerIntegration, PriorityBypass)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::vector<int> order;
        std::mutex order_mutex;

        scheduler.start();

        for (int i = 0; i < 50; ++i)
            scheduler.submit(new PriorityTrackingTask(0, order, order_mutex));
        scheduler.submit(new PriorityTrackingTask(100, order, order_mutex));

        scheduler.arm_for_execution();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(order_mutex);
                if (order.size() >= 51)
                    break;
            }
            if (std::chrono::steady_clock::now() > deadline)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        scheduler.stop();

        ASSERT_EQ(order.size(), 51u);

        int window = static_cast<int>(scheduler.worker_count()) * 2;
        if (window > 51)
            window = 51;

        bool found_high_early = false;
        for (int i = 0; i < window; ++i)
        {
            if (order[i] == 100)
            {
                found_high_early = true;
                break;
            }
        }

        EXPECT_TRUE(found_high_early) << "High priority task missed the first batch.";
    }
} // namespace SchedulerIntegrationTests
