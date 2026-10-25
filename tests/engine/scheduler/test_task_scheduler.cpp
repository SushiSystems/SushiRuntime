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
 * @file test_task_scheduler.cpp
 * @brief Tests the TaskScheduler: node placement, its work-stealing deque and its offload seam.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/device/device_descriptor.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/topology_provider.hpp>
#include <SushiRuntime/topology/topology_view.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/offload_state.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>
#include <SushiRuntime/probe_tasks/atomic_counter_task.hpp>
#include <SushiRuntime/probe_tasks/priority_tracking_task.hpp>
#include <SushiRuntime/runtime_assembly/context_factory.hpp>
#include <SushiRuntime/runtime_assembly/scheduler_factory.hpp>


/** @brief Tests which device queue the scheduler hands a graph node at dispatch. */
namespace NodePlacementTests
{
    namespace
    {
        using SushiRuntime::make_sushi;
        using SushiRuntime::sushi_ptr;
        using SushiRuntime::Internal::Graph::Node;

        /** @brief Stands for "handed a queue of no entry", and for a node whose work did not run. */
        constexpr std::size_t K_NO_DEVICE = static_cast<std::size_t>(-1);

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
            node->mode = Node::ExecutionMode::Host;
            node->host_work = [&handed](sycl::queue& queue, const std::vector<sycl::event>&)
            {
                handed = queue;
                return sycl::event{};
            };
            return node;
        }
    } // namespace

    /** @brief Runs a two-worker scheduler over a two-entry device set; see README.md. */
    class Medium_NodePlacement : public ::testing::Test
    {
        protected:
            Medium_NodePlacement()
                : topology_(SingleNodeTopology(), std::make_shared<SingleNodeDistance>()),
                  devices_(two_entries_over_the_default_device(), false),
                  memory_(devices_.plain_queues()),
                  scheduler_(SushiRuntime::Internal::Scheduler::WorkerPoolSettings{
                                 2, 0, SushiRuntime::PinPolicy::Float},
                             topology_, devices_, memory_)
            {
                scheduler_.start();
                scheduler_.arm_for_execution();
            }

            ~Medium_NodePlacement() override
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
    TEST_F(Medium_NodePlacement, RegionlessNodesRunOnDeviceZeroAndAPlacedNodeOnItsDevice)
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
    TEST_F(Medium_NodePlacement, ANodeKeepsItsDeviceOnEveryStep)
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
     * @brief A node over an owned region runs on the owner unless it states a device in the
     *        owner's context, which then wins.
     */
    TEST_F(Medium_NodePlacement, AStatedPlacementInTheOwnersContextOutranksTheOwner)
    {
        int cell = 0;
        memory_.adopt(&cell, 1, sizeof(cell), std::nullopt);

        std::optional<sycl::queue> handed;
        const sushi_ptr<Node> unplaced = make_recording_node(handed);
        unplaced->io_writes_.emplace_back(&cell);
        EXPECT_EQ(dispatched_device(*unplaced, handed), 1u);

        const sushi_ptr<Node> placed = make_recording_node(handed);
        placed->io_writes_.emplace_back(&cell);
        placed->place_on(0);
        EXPECT_EQ(dispatched_device(*placed, handed), 0u);

        memory_.forget(&cell);
    }

    /**
     * @brief A node whose region no device owns runs on the placement it states, else on device 0.
     */
    TEST_F(Medium_NodePlacement, AnUnownedRegionLeavesTheStatedPlacementInForce)
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
    TEST_F(Medium_NodePlacement, APlacementPastTheDeviceSetFailsTheRun)
    {
        std::optional<sycl::queue> handed;
        const sushi_ptr<Node> node = make_recording_node(handed);
        node->metadata.name = "misplaced";
        node->place_on(2);

        EXPECT_EQ(dispatched_device(*node, handed), K_NO_DEVICE);
        ASSERT_TRUE(scheduler_.run_failed());

        const std::optional<SushiRuntime::Error> error = scheduler_.run_error();
        ASSERT_TRUE(error.has_value());
        const std::string& message = error->message;
        EXPECT_EQ(error->code, SushiRuntime::ErrorCode::InvalidGraph) << message;
        EXPECT_NE(message.find("node 'misplaced'"), std::string::npos) << message;
        EXPECT_NE(message.find("device 2"), std::string::npos) << message;
        EXPECT_NE(message.find("2 device(s)"), std::string::npos) << message;
    }
} // namespace NodePlacementTests

/** @brief Tests what the scheduler offers and reports to an offload coordinator. */
namespace OffloadSeamTests
{
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

                void prepare(SushiRuntime::span<const sushi_ptr<Node>>,
                             const std::shared_ptr<const SushiRuntime::PlacementRules>&) override
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
    class Medium_OffloadSeam : public ::testing::Test
    {
        protected:
            Medium_OffloadSeam()
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

            ~Medium_OffloadSeam() override
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
    TEST_F(Medium_OffloadSeam, AbortedRunOffersNoNode)
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
    TEST_F(Medium_OffloadSeam, SkippedNodeNotReportedAsCompleted)
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
    TEST_F(Medium_OffloadSeam, OnlyAcceptedOffersAreCounted)
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
} // namespace OffloadSeamTests

/** @brief Tests the scheduler for exact task execution, priority bypass and a clean stop. */
namespace SchedulerIntegrationTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using SushiRuntime::Test::AtomicCounterTask;
    using SushiRuntime::Test::PriorityTrackingTask;

    namespace
    {
        /**
         * @brief Blocks until value hits target, or fails past the budget.
         * @param value The atomic value to wait for.
         * @param target The target value.
         * @param budget The maximum duration to wait.
         * @return True if the target was reached, false if it timed out.
         */
        bool wait_for(const std::atomic<size_t>& value, size_t target,
                      std::chrono::seconds budget)
        {
            auto deadline = std::chrono::steady_clock::now() + budget;
            while (value.load(std::memory_order_acquire) < target)
            {
                if (std::chrono::steady_clock::now() > deadline)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return true;
        }
    }

    /**
     * @brief Tests that a modest batch of host tasks runs exactly once.
     *
     * Verifies that the scheduler properly executes all submitted tasks.
     */
    TEST(Medium_SchedulerIntegration, ExecutesExactlySubmitted)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed{0};
        constexpr size_t TOTAL = 2000;

        scheduler.start();
        scheduler.arm_for_execution();

        for (size_t i = 0; i < TOTAL; ++i)
            scheduler.submit(new AtomicCounterTask(executed));

        ASSERT_TRUE(wait_for(executed, TOTAL, std::chrono::seconds(10)))
            << "Scheduler did not drain a 2000-task batch in time.";

        scheduler.stop();
        EXPECT_EQ(executed.load(), TOTAL);
    }

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

    /**
     * @brief Tests that a single lifecycle round shuts down cleanly.
     *
     * Verifies that a clean arm -> submit -> stop round with no inflight work exits without hanging.
     */
    TEST(Medium_SchedulerIntegration, CleanLifecycleRound)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler scheduler = make_scheduler(ctx);

        std::atomic<size_t> executed{0};

        scheduler.start();
        scheduler.arm_for_execution();
        for (int i = 0; i < 100; ++i)
            scheduler.submit(new AtomicCounterTask(executed));

        ASSERT_TRUE(wait_for(executed, 100, std::chrono::seconds(5)));
        EXPECT_NO_THROW({ scheduler.stop(); });
        EXPECT_EQ(executed.load(), 100u);
    }
} // namespace SchedulerIntegrationTests

/** @brief Tests that node-local steal victims precede remote ones under a synthetic NUMA topology. */
namespace NUMAStealTests
{
    using namespace SushiRuntime::Internal::Execution;
    using namespace SushiRuntime::Internal::Scheduler;
    using SushiRuntime::Test::make_scheduler;
    using namespace SushiRuntime::Internal::Topology;

    namespace
    {
        /**
         * @brief Creates a synthetic RuntimeContext split into `nodes` NUMA nodes.
         *
         * Generates a context with a symmetric distance matrix mapping core i -> i % nodes.
         *
         * @param nodes The number of synthetic NUMA nodes.
         * @return A shared pointer to the created RuntimeContext.
         */
        std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
        {
            auto provider = std::make_shared<RemappingTopologyProvider>(
                std::make_shared<HwlocTopologyProvider>(), nodes);

            std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
            for (int i = 0; i < nodes; ++i)
                matrix[i][i] = 10;
            auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));

            return SushiRuntime::Test::make_context({}, provider, distance);
        }
    }

    /**
     * @brief Tests that node-local victims precede remote ones in the steal order.
     *
     * With cores split across nodes, every worker's steal order must list all
     * same-node victims before any cross-node victim.
     */
    TEST(Medium_NumaStealTest, LocalVictimsPrecedeRemote)
    {
        constexpr int NODES = 2;
        auto ctx = make_synthetic_ctx(NODES);

        TaskScheduler sched = make_scheduler(*ctx); // construction only — workers are not started.

        const int workers = static_cast<int>(sched.worker_count());
        if (workers < 2)
            GTEST_SKIP() << "Need at least 2 worker threads to exercise stealing order.";

        for (int th = 0; th < workers; ++th)
        {
            const int my_node = th % NODES;
            const std::vector<int> order = sched.steal_victim_order(th);

            // The worker steals from everyone except itself.
            ASSERT_EQ(order.size(), static_cast<size_t>(workers - 1))
                << "Victim list for worker " << th << " has wrong size.";

            // Once the first remote victim appears, no local victim may follow.
            bool seen_remote = false;
            for (int victim : order)
            {
                const bool is_local = (victim % NODES) == my_node;
                if (!is_local)
                    seen_remote = true;
                else
                    EXPECT_FALSE(seen_remote)
                        << "Worker " << th << ": local victim " << victim
                        << " appears after a remote victim — locality violated.";
            }
        }
    }

    /**
     * @brief Tests that a flat topology keeps all victims in the steal order.
     *
     * The default (real) topology collapses to one node here; the steal order
     * must still contain every other worker.
     */
    TEST(Medium_NumaStealTest, FlatTopologyKeepsAllVictims)
    {
        const auto ctx_owner = SushiRuntime::Test::make_context();
        RuntimeContext& ctx = *ctx_owner;
        TaskScheduler sched = make_scheduler(ctx);

        const int workers = static_cast<int>(sched.worker_count());
        if (workers < 2)
            GTEST_SKIP() << "Need at least 2 worker threads.";

        const std::vector<int> order = sched.steal_victim_order(0);
        EXPECT_EQ(order.size(), static_cast<size_t>(workers - 1));

        // No self, no duplicates, all in range.
        std::vector<bool> seen(workers, false);
        for (int v : order)
        {
            ASSERT_GE(v, 0);
            ASSERT_LT(v, workers);
            EXPECT_NE(v, 0) << "Worker 0 must not be its own steal victim.";
            EXPECT_FALSE(seen[v]) << "Duplicate victim " << v;
            seen[v] = true;
        }
    }
} // namespace NUMAStealTests

/**
 * @brief Tests that stop() right after submit drains cleanly.
 *
 * Prevents deadlocks or leaked threads when stopping the scheduler immediately
 * after queuing tasks.
 */
TEST(Medium_TaskScheduler, AbruptStopDrains)
{
    const auto ctx_owner = SushiRuntime::Test::make_context();
    SushiRuntime::Internal::Execution::RuntimeContext& ctx = *ctx_owner;
    SushiRuntime::Internal::Scheduler::TaskScheduler scheduler =
        SushiRuntime::Test::make_scheduler(ctx);
    std::atomic<size_t> executed{0};

    scheduler.start();
    scheduler.arm_for_execution();

    for (int i = 0; i < 1000; ++i)
        scheduler.submit(new SushiRuntime::Test::AtomicCounterTask(executed));

    EXPECT_NO_THROW({ scheduler.stop(); })
        << "Abrupt stop() must drain without deadlock or throw.";
}
