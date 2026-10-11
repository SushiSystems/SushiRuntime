/**************************************************************************/
/* test_distributed_jepsen.cpp                                            */
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
 * @file test_distributed_jepsen.cpp
 * @brief Tests that an offload graph under seeded message faults ends equal to a clean run.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/message_type.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/execution/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;
using namespace SushiRuntime::Internal::Graph;
using namespace SushiRuntime::Literals;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the doubling operation: out[i] = in[i] * 2. */
    constexpr auto K_VEC_DOUBLE = "vec.double"_op;

    /** @brief Holds the elements of each buffer. */
    constexpr std::size_t K_COUNT = 128;

    /** @brief Holds how long a test waits for a cluster to assemble. */
    constexpr std::chrono::seconds K_WAIT{10};

    /** @brief Holds how long a run under faults may take before it counts as hung. */
    constexpr std::chrono::seconds K_RUN_LIMIT{30};

    /** @brief Holds the tolerance of every float comparison; see README.md. */
    constexpr float K_TOLERANCE = 0.0f;

    /** @brief Holds the value an output holds before any task wrote it. */
    constexpr float K_UNWRITTEN = -1.0f;

    /** @brief Holds the wait between two flushes of the frames the fault policy parked. */
    constexpr std::chrono::milliseconds K_FLUSH_PERIOD{25};

    /** @brief Holds how long the nemesis keeps one worker's link cut. */
    constexpr std::chrono::milliseconds K_PARTITION_LENGTH{60};

    /** @brief Holds the value mixed into a seed for the nemesis's own random source. */
    constexpr std::uint64_t K_NEMESIS_SALT = 0x9e3779b97f4a7c15ull;

    /** @brief Returns the value in[i] holds: i + 1. */
    constexpr float input_value(std::size_t index) noexcept
    {
        return static_cast<float>(index) + 1.0f;
    }

    /**
     * @brief Owns one shared USM allocation of K_COUNT floats on a queue.
     */
    class USMFloats final
    {
        public:
            /**
             * @brief Allocates the floats and sets each to @p value.
             * @throws std::runtime_error When the device gives no memory.
             */
            USMFloats(sycl::queue& queue, float value)
                : queue_(queue), data_(sycl::malloc_shared<float>(K_COUNT, queue))
            {
                if (data_ == nullptr)
                    throw std::runtime_error("sycl::malloc_shared returned null for a test buffer");
                std::fill_n(data_, K_COUNT, value);
            }

            /** @brief Frees the allocation. */
            ~USMFloats()
            {
                sycl::free(data_, queue_);
            }

            USMFloats(const USMFloats&) = delete;
            USMFloats& operator=(const USMFloats&) = delete;
            USMFloats(USMFloats&&) = delete;
            USMFloats& operator=(USMFloats&&) = delete;

            /** @brief Returns the base address. */
            [[nodiscard]] float* data() const noexcept
            {
                return data_;
            }

        private:
            /// @brief The queue the allocation belongs to.
            sycl::queue& queue_;

            /// @brief The base address.
            float* data_;
    };

    /** @brief Returns an input buffer whose element i holds input_value(i). */
    std::unique_ptr<USMFloats> make_input(sycl::queue& queue)
    {
        auto input = std::make_unique<USMFloats>(queue, 0.0f);
        for (std::size_t index = 0; index < K_COUNT; ++index)
            input->data()[index] = input_value(index);
        return input;
    }

    /** @brief Returns the index of the first element that is not twice its input, or K_COUNT. */
    std::size_t first_wrong_element(const USMFloats& output)
    {
        for (std::size_t index = 0; index < K_COUNT; ++index)
            if (std::abs(output.data()[index] - 2.0f * input_value(index)) > K_TOLERANCE)
                return index;
        return K_COUNT;
    }

    /**
     * @brief Returns a registry that holds the doubling kernel.
     * @param runs Counts the kernel's runs; outlives the registry.
     */
    sushi_ptr<KernelRegistry> vec_double_kernels(std::atomic<int>& runs)
    {
        sushi_ptr<KernelRegistry> kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(
            K_VEC_DOUBLE,
            [&runs](sycl::queue& queue, span<const BufferView> inputs,
                    span<const BufferView> outputs, const NodeOptions&) -> sycl::event
            {
                const float* in = inputs[0].as<const float>();
                float* out = outputs[0].as<float>();
                const std::size_t count = outputs[0].size_bytes / sizeof(float);
                std::atomic<int>* counter = &runs;

                return queue.submit([&](sycl::handler& handler)
                {
                    SushiRuntime::Detail::enqueue_host_task(handler, [=]
                    {
                        for (std::size_t index = 0; index < count; ++index)
                            out[index] = in[index] * 2.0f;
                        counter->fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
        return kernels;
    }

    /** @brief Adds an op-tagged task out = in * 2 whose local body computes the same. */
    void add_vec_double(Graph& graph, const USMFloats& input, const USMFloats& output)
    {
        NodeOptions options;
        options.name = "double";
        options.operation = K_VEC_DOUBLE;

        float* in = input.data();
        float* out = output.data();
        graph.add_task(
            Reads(BufferRegion(in)), Writes(BufferRegion(out)),
            [in, out](sycl::queue& queue, const std::vector<sycl::event>&) -> sycl::event
            {
                return queue.submit([&](sycl::handler& handler)
                {
                    SushiRuntime::Detail::enqueue_host_task(handler, [=]
                    {
                        for (std::size_t index = 0; index < K_COUNT; ++index)
                            out[index] = in[index] * 2.0f;
                    });
                });
            }, options);
    }

    /**
     * @brief Runs a graph once on its own thread, so a run that never returns is
     *        reported and not waited for.
     */
    class BackgroundRun final
    {
        public:
            /** @brief Starts the run; @p graph outlives this object. */
            explicit BackgroundRun(Graph& graph)
            {
                auto done = std::make_shared<std::promise<void>>();
                finished_ = done->get_future();
                thread_ = std::thread([&graph, done]
                {
                    graph.run().value();
                    done->set_value();
                });
            }

            /** @brief Joins a run that returned and detaches one that did not. */
            ~BackgroundRun()
            {
                if (!thread_.joinable())
                    return;

                if (finished_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    thread_.join();
                else
                    thread_.detach();
            }

            BackgroundRun(const BackgroundRun&) = delete;
            BackgroundRun& operator=(const BackgroundRun&) = delete;
            BackgroundRun(BackgroundRun&&) = delete;
            BackgroundRun& operator=(BackgroundRun&&) = delete;

            /** @brief Returns whether the run returned within @p limit, and joins it if so. */
            [[nodiscard]] bool wait(std::chrono::milliseconds limit)
            {
                if (finished_.wait_for(limit) != std::future_status::ready)
                    return false;

                if (thread_.joinable())
                    thread_.join();
                return true;
            }

        private:
            /// @brief Becomes ready when execute() returned.
            std::future<void> finished_;

            /// @brief The thread that calls execute().
            std::thread thread_;
    };

    /**
     * @brief Runs one real worker: its own runtime and the agent that serves a fabric.
     */
    class LocalWorker final
    {
        public:
            /** @brief Builds the worker on the next worker endpoint of @p fabric. */
            LocalWorker(InProcessFabric& fabric, std::atomic<int>& runs,
                        WorkerAgentOptions options)
                : agent_(runtime_, InProcessTransport::make_worker(fabric),
                         vec_double_kernels(runs), std::move(options))
            {

            }

            /** @brief Starts the agent and returns whether its transport started. */
            [[nodiscard]] bool start()
            {
                return agent_.start().is_ok();
            }

            /** @brief Stops the agent. */
            void stop() noexcept
            {
                agent_.stop();
            }

        private:
            /// @brief The worker's own runtime; declared first, destroyed last.
            Runtime runtime_ = Runtime::create().value();

            /// @brief The agent under test.
            WorkerAgent agent_;
    };

    /** @brief Returns worker options with a short HELLO retry and a short pull timeout. */
    WorkerAgentOptions impatient_worker()
    {
        WorkerAgentOptions options;
        options.heartbeat_interval = std::chrono::milliseconds(40);
        options.pull_idle_timeout = std::chrono::milliseconds(120);
        return options;
    }

    /** @brief Returns master options that offload any task and give up on a worker quickly. */
    ClusterOptions impatient_master()
    {
        ClusterOptions options;
        options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
        options.liveness_timeout = std::chrono::milliseconds(250);
        options.task_deadline = std::chrono::milliseconds(300);
        return options;
    }

    /**
     * @brief Returns a policy that drops 8 frames in 100, repeats 4 and parks 8, by @p seed.
     *
     * The fabric calls a policy from several threads at once, so the policy locks its
     * random source.
     */
    FaultPolicy seeded_faults(std::uint64_t seed)
    {
        /** @brief Holds the random source of one policy and the mutex that guards it. */
        struct Source
        {
            /** @brief Guards random. */
            std::mutex mutex;

            /** @brief Draws the fate of each frame. */
            std::mt19937_64 random;
        };

        auto source = std::make_shared<Source>();
        source->random.seed(seed);
        return [source](const FaultedFrame&)
        {
            const std::lock_guard<std::mutex> lock(source->mutex);
            const std::uint64_t draw = source->random() % 100;
            if (draw < 8)
                return FaultAction::Drop;
            if (draw < 12)
                return FaultAction::Duplicate;
            if (draw < 20)
                return FaultAction::Hold;
            return FaultAction::Deliver;
        };
    }

    /**
     * @brief Returns a policy that drops the first frame of each given type and delivers
     *        every other frame.
     * @param dropped Counts the frames the policy dropped.
     */
    FaultPolicy drop_first_of(const std::vector<MessageType>& types,
                              std::shared_ptr<std::atomic<int>> dropped)
    {
        auto is_owed = std::make_shared<std::array<std::atomic<bool>, K_MESSAGE_TYPE_COUNT + 1>>();
        for (std::atomic<bool>& flag : *is_owed)
            flag.store(false);
        for (const MessageType type : types)
            (*is_owed)[static_cast<std::size_t>(type)].store(true);

        return [is_owed, dropped = std::move(dropped)](const FaultedFrame& frame)
        {
            if (!(*is_owed)[static_cast<std::size_t>(frame.type)].exchange(false))
                return FaultAction::Deliver;

            dropped->fetch_add(1, std::memory_order_relaxed);
            return FaultAction::Drop;
        };
    }

    /**
     * @brief Puts a fabric under a seeded fault policy and, on its own thread, flushes
     *        parked frames in a shuffled order and cuts one worker's link now and then.
     */
    class Nemesis final
    {
        public:
            /** @brief Installs the policy and starts the thread. */
            Nemesis(InProcessFabric& fabric, std::uint64_t seed, std::size_t worker_count)
                : fabric_(fabric), worker_count_(worker_count)
            {
                fabric_.set_fault_policy(seeded_faults(seed));
                thread_ = std::thread(&Nemesis::run, this, seed ^ K_NEMESIS_SALT);
            }

            /** @brief Heals the fabric if heal() was not called. */
            ~Nemesis()
            {
                heal();
            }

            Nemesis(const Nemesis&) = delete;
            Nemesis& operator=(const Nemesis&) = delete;
            Nemesis(Nemesis&&) = delete;
            Nemesis& operator=(Nemesis&&) = delete;

            /** @brief Joins the thread, restores every link, removes the policy and flushes. */
            void heal()
            {
                {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    stopping_ = true;
                }
                changed_.notify_all();
                if (thread_.joinable())
                    thread_.join();

                for (std::size_t worker = 0; worker < worker_count_; ++worker)
                    fabric_.set_partitioned(worker, false);
                fabric_.set_fault_policy(FaultPolicy());
                fabric_.flush_held();
            }

        private:
            /** @brief Waits for @p length and returns whether the nemesis should keep running. */
            [[nodiscard]] bool pause(std::chrono::milliseconds length)
            {
                std::unique_lock<std::mutex> lock(mutex_);
                return !changed_.wait_for(lock, length, [&] { return stopping_; });
            }

            /** @brief Flushes and partitions until heal() asks the thread to end. */
            void run(std::uint64_t seed)
            {
                std::mt19937_64 random(seed);
                while (pause(K_FLUSH_PERIOD))
                {
                    fabric_.flush_held(random());
                    if ((random() & 7u) != 0)
                        continue;

                    const std::size_t worker = random() % worker_count_;
                    fabric_.set_partitioned(worker, true);
                    const bool keeps_running = pause(K_PARTITION_LENGTH);
                    fabric_.set_partitioned(worker, false);
                    if (!keeps_running)
                        return;
                }
            }

            /// @brief The fabric under faults.
            InProcessFabric& fabric_;

            /// @brief The number of worker endpoints the nemesis may cut.
            std::size_t worker_count_;

            /// @brief Guards stopping_.
            std::mutex mutex_;

            /// @brief Wakes a pause when heal() is called.
            std::condition_variable changed_;

            /// @brief Set by heal().
            bool stopping_ = false;

            /// @brief The thread that flushes and partitions.
            std::thread thread_;
    };

    /**
     * @brief Runs four independent offloadable tasks on two workers under the faults of one
     *        seed.
     * @return An empty string when the run returned and every output is right, else the reason.
     */
    std::string run_one_scenario(std::uint64_t seed)
    {
        constexpr std::size_t K_WORKERS = 2;
        constexpr std::size_t K_TASKS = 4;

        std::atomic<int> worker_runs{0};
        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context =
            SushiRuntime::Detail::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        const std::unique_ptr<USMFloats> input = make_input(queue);
        std::vector<std::unique_ptr<USMFloats>> outputs;
        for (std::size_t task = 0; task < K_TASKS; ++task)
            outputs.push_back(std::make_unique<USMFloats>(queue, K_UNWRITTEN));

        InProcessFabric fabric;
        LocalWorker first(fabric, worker_runs, impatient_worker());
        LocalWorker second(fabric, worker_runs, impatient_worker());
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          impatient_master()).value();

        // The cluster assembles before any fault is injected; see README.md.
        if (!first.start() || !second.start() || !cluster.wait_for_workers(K_WORKERS, K_WAIT))
            return "the cluster did not assemble";

        OffloadCoordinator* master = master_context.offload();
        cluster.share(input->data(), K_COUNT * sizeof(float));
        auto graph = master_runtime.graph();
        for (const std::unique_ptr<USMFloats>& output : outputs)
        {
            cluster.share(output->data(), K_COUNT * sizeof(float));
            add_vec_double(graph, *input, *output);
        }
        // The plan is built before the first fault, while both workers are known.
        graph.compile().value();

        Nemesis nemesis(fabric, seed, K_WORKERS);
        BackgroundRun run(graph);
        const bool returned = run.wait(K_RUN_LIMIT);
        nemesis.heal();

        // Stopping the master returns every node it holds, so a hung run can still end.
        if (!returned)
            master->stop();
        const bool ended = returned || run.wait(K_WAIT);

        // No worker and no master thread writes an output once all three have stopped.
        first.stop();
        second.stop();
        cluster.detach();

        if (!returned)
            return ended ? "execute() did not return under faults until the master was stopped"
                         : "execute() did not return under faults, nor after the master stopped";

        for (std::size_t task = 0; task < K_TASKS; ++task)
        {
            const std::size_t wrong = first_wrong_element(*outputs[task]);
            if (wrong != K_COUNT)
                return "output " + std::to_string(task) + " element " + std::to_string(wrong) +
                       " is " + std::to_string(outputs[task]->data()[wrong]);
        }
        return std::string();
    }

    /** @brief Holds what one run on a one-worker cluster with a lossy handshake left behind. */
    struct LossyRegistration
    {
        bool assembled = false;       /**< The master counted the worker as registered. */
        bool returned = false;        /**< execute() returned within the limit. */
        int frames_dropped = 0;       /**< Frames the fault policy dropped. */
        int worker_runs = 0;          /**< Runs of the worker's kernel. */
        std::size_t wrong = K_COUNT;  /**< The first wrong output element, or K_COUNT. */
    };

    /**
     * @brief Registers one worker while the first frame of each given type is lost, then
     *        runs one offloadable task.
     */
    LossyRegistration register_under_loss(const std::vector<MessageType>& lost_types)
    {
        LossyRegistration outcome;
        std::atomic<int> worker_runs{0};
        const auto dropped = std::make_shared<std::atomic<int>>(0);

        Runtime master_runtime = Runtime::create().value();
        RuntimeContext& master_context =
            SushiRuntime::Detail::ContextAccess::context(master_runtime);
        sycl::queue& queue = master_context.devices().queue(0);
        const std::unique_ptr<USMFloats> input = make_input(queue);
        USMFloats output(queue, K_UNWRITTEN);

        InProcessFabric fabric;
        fabric.set_fault_policy(drop_first_of(lost_types, dropped));

        LocalWorker worker(fabric, worker_runs, impatient_worker());
        Cluster cluster = Cluster::attach(master_runtime, InProcessTransport::make_master(fabric),
                                          impatient_master()).value();
        outcome.assembled = worker.start() && cluster.wait_for_workers(1, K_WAIT);
        if (!outcome.assembled)
        {
            outcome.frames_dropped = dropped->load(std::memory_order_relaxed);
            return outcome;
        }

        OffloadCoordinator* master = master_context.offload();
        cluster.share(input->data(), K_COUNT * sizeof(float));
        cluster.share(output.data(), K_COUNT * sizeof(float));
        auto graph = master_runtime.graph();
        add_vec_double(graph, *input, output);
        graph.compile().value();

        BackgroundRun run(graph);
        outcome.returned = run.wait(K_RUN_LIMIT);
        if (!outcome.returned)
            master->stop();

        worker.stop();
        cluster.detach();

        // The master counts the worker before its WELCOME leaves, so the drop is read last.
        outcome.frames_dropped = dropped->load(std::memory_order_relaxed);
        outcome.worker_runs = worker_runs.load(std::memory_order_relaxed);
        if (outcome.returned)
            outcome.wrong = first_wrong_element(output);
        return outcome;
    }
} // namespace

/** @brief Tests that eight seeded fault schedules each end with the outputs of a clean run. */
TEST(Integration_DistributedJepsenTest, DeterminismUnderRandomFaults)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    for (std::uint64_t seed = 1; seed <= 8; ++seed)
    {
        const std::string failure = run_one_scenario(seed);
        EXPECT_TRUE(failure.empty()) << "seed " << seed << ": " << failure;
    }
}

/** @brief Tests that a worker registers although HELLO, CHALLENGE and PROOF are each lost once. */
TEST(Integration_DistributedJepsenTest, RegistrationSurvivesMessageLoss)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const LossyRegistration outcome = register_under_loss(
        {MessageType::Hello, MessageType::Challenge, MessageType::Proof});

    ASSERT_TRUE(outcome.assembled) << "the worker never registered after the lost frames";
    EXPECT_EQ(outcome.frames_dropped, 3) << "the handshake did not meet each of the three losses";
    ASSERT_TRUE(outcome.returned) << "execute() did not return after a lossy registration";
    EXPECT_EQ(outcome.worker_runs, 1) << "the registered worker did not run the task";
    EXPECT_EQ(outcome.wrong, K_COUNT) << "output element " << outcome.wrong << " is wrong";
}

/** @brief Tests that a worker whose WELCOME is lost registers again and runs the task. */
TEST(Integration_DistributedJepsenTest, RegistrationSurvivesALostWelcome)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const LossyRegistration outcome = register_under_loss({MessageType::Welcome});

    ASSERT_TRUE(outcome.assembled);
    EXPECT_EQ(outcome.frames_dropped, 1);
    ASSERT_TRUE(outcome.returned) << "execute() did not return after the lost WELCOME";
    EXPECT_EQ(outcome.wrong, K_COUNT) << "output element " << outcome.wrong << " is wrong";
    EXPECT_EQ(outcome.worker_runs, 1)
        << "the master counts a worker that never learned it was registered";
}
