/**************************************************************************/
/* benchmark_distributed_offload.cpp                                      */
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
 * @file benchmark_distributed_offload.cpp
 * @brief Measures the offload round trip per task and per buffer size, in process and over TCP.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>

#include "measurement.hpp"
#include "test_helpers.hpp"
#include "test_context.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using namespace SushiRuntime::Literals;
using SushiRuntime::Internal::Execution::RuntimeContext;
using SushiRuntime::Test::MeasurementLabel;
using SushiRuntime::Test::RunStatistics;

namespace
{
    /** @brief Names the kernel that writes each input element plus one to the output. */
    constexpr OperationID K_INCREMENT = "benchmark.increment"_op;

    /** @brief Bounds every wait on a condition, so a broken cluster fails a test and hangs none. */
    constexpr std::chrono::seconds K_WAIT{60};

    /** @brief Counts the independent tasks of the many-task case. */
    constexpr std::size_t K_SMALL_TASK_COUNT = 256;

    /** @brief Counts the floats each buffer of the many-task case holds. */
    constexpr std::size_t K_SMALL_TASK_FLOATS = 256;

    /** @brief Holds the period of the pattern written to an input, so every value is exact. */
    constexpr std::size_t K_PATTERN_PERIOD = 1024;

    /** @brief Runs K_INCREMENT: output[i] = input[i] + 1 over the output's element count. */
    sycl::event increment(sycl::queue& queue, span<const BufferView> inputs,
                          span<const BufferView> outputs, const NodeOptions&)
    {
        const float* source = inputs[0].as<const float>();
        float* target = outputs[0].as<float>();
        const std::size_t count = outputs[0].size_bytes / sizeof(float);

        return queue.parallel_for(sycl::range<1>(count), [source, target](sycl::id<1> id)
        {
            target[id[0]] = source[id[0]] + 1.0f;
        });
    }

    /** @brief Returns a registry that holds the kernel of K_INCREMENT. */
    sushi_ptr<KernelRegistry> make_kernels()
    {
        auto kernels = make_sushi<KernelRegistry>();
        kernels->register_kernel(K_INCREMENT, increment);
        return kernels;
    }

    /** @brief Holds the master end of one kind of transport and the way to make a worker end. */
    struct TransportPair
    {
        /** @brief Names the kind in a measurement line; holds no whitespace. */
        const char* name = "";

        /** @brief Holds the master end, not started. */
        std::shared_ptr<Transport> master;

        /**
         * @brief Makes one worker end that reaches the master.
         * @pre The master end has started, so its address is known.
         */
        std::function<std::shared_ptr<Transport>()> make_worker;
    };

    /** @brief Returns the two ends of an in-process fabric. */
    TransportPair make_in_process_pair()
    {
        auto fabric = std::make_shared<InProcessFabric>();

        TransportPair pair;
        pair.name = "in_process";
        pair.master = InProcessTransport::make_master(*fabric);
        pair.make_worker = [fabric] { return InProcessTransport::make_worker(*fabric); };
        return pair;
    }

    /** @brief Returns a TCP master on a free loopback port and workers that connect to it. */
    TransportPair make_tcp_loopback_pair()
    {
        auto master = TCPTransport::make_master();

        TransportPair pair;
        pair.name = "tcp_loopback";
        pair.master = master;
        pair.make_worker = [master] { return TCPTransport::make_worker(master->local_endpoint()); };
        return pair;
    }

    /** @brief Runs one worker agent on a runtime of its own, inside this process. */
    class LocalWorker final
    {
        public:
            /** @brief Builds the worker's runtime and starts the agent on @p transport. */
            LocalWorker(std::shared_ptr<Transport> transport, sushi_ptr<KernelRegistry> kernels)
                : agent_(context_, std::move(transport), std::move(kernels))
            {
                started_ = agent_.start();
            }

            /** @brief Stops the agent before its runtime goes. */
            ~LocalWorker()
            {
                agent_.stop();
            }

            LocalWorker(const LocalWorker&) = delete;
            LocalWorker& operator=(const LocalWorker&) = delete;
            LocalWorker(LocalWorker&&) = delete;
            LocalWorker& operator=(LocalWorker&&) = delete;

            /** @brief Returns how the agent started. */
            [[nodiscard]] const TransportResult& started() const noexcept
            {
                return started_;
            }

            /** @brief Returns the number of kernels this worker ran to the end. */
            [[nodiscard]] std::uint64_t tasks_run() const noexcept
            {
                return agent_.statistics().tasks_run;
            }

        private:
            /// @brief The runtime whose device 0 runs the kernels and holds the cache.
            const std::shared_ptr<RuntimeContext> context_owner_ =
                SushiRuntime::Test::make_context();

            /// @brief The context context_owner_ holds.
            RuntimeContext& context_ = *context_owner_;

            /// @brief The agent; declared after the runtime it uses.
            WorkerAgent agent_;

            /// @brief How the agent started.
            TransportResult started_;
    };

    /** @brief Holds a master runtime with a cluster attached and the workers that serve it. */
    class OffloadRig final
    {
        public:
            /**
             * @brief Attaches a master on the pair's master end, then starts the workers.
             * @pre The master starts; the process stops, naming the error, when it does not.
             */
            OffloadRig(const TransportPair& transports, std::size_t worker_count)
                : kernels_(make_kernels()),
                  cluster_(Cluster::attach(master_, transports.master, master_options(kernels_)).value())
            {
                workers_.reserve(worker_count);
                for (std::size_t index = 0; index < worker_count; ++index)
                    workers_.push_back(
                        std::make_unique<LocalWorker>(transports.make_worker(), kernels_));
            }

            /** @brief Stops the workers, then the master with its runtime. */
            ~OffloadRig() = default;

            OffloadRig(const OffloadRig&) = delete;
            OffloadRig& operator=(const OffloadRig&) = delete;
            OffloadRig(OffloadRig&&) = delete;
            OffloadRig& operator=(OffloadRig&&) = delete;

            /** @brief Waits until every worker is registered and the master knows the kernel. */
            [[nodiscard]] testing::AssertionResult wait_until_ready()
            {
                for (const std::unique_ptr<LocalWorker>& worker : workers_)
                {
                    if (!worker->started().is_ok())
                        return testing::AssertionFailure()
                               << "a worker did not start: " << worker->started().message;
                }
                if (!cluster_.wait_for_workers(workers_.size(), K_WAIT))
                    return testing::AssertionFailure()
                           << cluster_.alive_workers() << " of " << workers_.size()
                           << " workers registered";
                if (!cluster_.supports(K_INCREMENT))
                    return testing::AssertionFailure() << "no worker registered the kernel";
                return testing::AssertionSuccess();
            }

            /** @brief Returns the master's runtime. */
            [[nodiscard]] RuntimeContext& master() noexcept
            {
                return master_;
            }

            /** @brief Returns the handle of the attached master. */
            [[nodiscard]] Cluster& cluster() noexcept
            {
                return cluster_;
            }

            /** @brief Returns the registry the master's local fallback reads. */
            [[nodiscard]] const KernelRegistry& kernels() const noexcept
            {
                return *kernels_;
            }

            /** @brief Returns the kernels each worker ran, in the order the workers were made. */
            [[nodiscard]] std::vector<std::uint64_t> tasks_run_per_worker() const
            {
                std::vector<std::uint64_t> counts;
                counts.reserve(workers_.size());
                for (const std::unique_ptr<LocalWorker>& worker : workers_)
                    counts.push_back(worker->tasks_run());
                return counts;
            }

        private:
            /** @brief Returns options that offload every task, whatever its size. */
            [[nodiscard]] static ClusterOptions master_options(sushi_ptr<KernelRegistry> kernels)
            {
                ClusterOptions options;
                options.policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
                options.kernels = std::move(kernels);
                return options;
            }

            /// @brief The master's runtime; declared first, so it is destroyed last.
            const std::shared_ptr<RuntimeContext> master_owner_ =
                SushiRuntime::Test::make_context();

            /// @brief The context master_owner_ holds.
            RuntimeContext& master_ = *master_owner_;

            /// @brief The kernels of the master and of every worker.
            sushi_ptr<KernelRegistry> kernels_;

            /// @brief The handle of the master attached to master_.
            Cluster cluster_;

            /// @brief The workers; destroyed first, which stops their agents.
            std::vector<std::unique_ptr<LocalWorker>> workers_;
    };

    /** @brief Owns one shared USM buffer of floats on the master and shares it for offload. */
    class SharedFloats final
    {
        public:
            /**
             * @brief Allocates @p count floats on device 0 and shares them with the master.
             * @pre @p cluster is attached to @p context, which outlives the buffer.
             */
            SharedFloats(RuntimeContext& context, Cluster& cluster, std::size_t count)
                : context_(&context), count_(count)
            {
                Internal::Memory::USMRequest request;
                request.kind = Internal::Memory::USMKind::Shared;
                request.bytes = bytes();
                data_ = static_cast<float*>(context.memory().allocate(request));
                cluster.share(data_, bytes());
            }

            /** @brief Releases the memory, which also removes it from the master's registry. */
            ~SharedFloats()
            {
                if (data_ != nullptr)
                    context_->memory().release(data_);
            }

            SharedFloats(const SharedFloats&) = delete;
            SharedFloats& operator=(const SharedFloats&) = delete;

            /** @brief Takes the memory of @p other and leaves it empty. */
            SharedFloats(SharedFloats&& other) noexcept
                : context_(other.context_), count_(other.count_),
                  data_(std::exchange(other.data_, nullptr))
            {

            }

            SharedFloats& operator=(SharedFloats&&) = delete;

            /** @brief Returns the first element. */
            [[nodiscard]] float* data() const noexcept
            {
                return data_;
            }

            /** @brief Returns the number of floats. */
            [[nodiscard]] std::size_t count() const noexcept
            {
                return count_;
            }

            /** @brief Returns the size in bytes. */
            [[nodiscard]] std::size_t bytes() const noexcept
            {
                return count_ * sizeof(float);
            }

            /** @brief Returns the view a registered task names the buffer by. */
            [[nodiscard]] BufferView view() const noexcept
            {
                return BufferView{data_, bytes()};
            }

        private:
            /// @brief The runtime whose allocator owns the memory.
            RuntimeContext* context_;

            /// @brief The number of floats.
            std::size_t count_;

            /// @brief The memory; null once moved from.
            float* data_ = nullptr;
    };

    /** @brief Holds the buffers of a set of independent increment tasks, one pair per task. */
    class IncrementBatch final
    {
        public:
            /** @brief Allocates one input and one output of @p floats_per_task for each task. */
            IncrementBatch(OffloadRig& rig, std::size_t task_count, std::size_t floats_per_task)
                : rig_(rig)
            {
                inputs_.reserve(task_count);
                outputs_.reserve(task_count);
                for (std::size_t task = 0; task < task_count; ++task)
                {
                    inputs_.emplace_back(rig.master(), rig.cluster(), floats_per_task);
                    outputs_.emplace_back(rig.master(), rig.cluster(), floats_per_task);
                }
            }

            /** @brief Returns the number of tasks. */
            [[nodiscard]] std::size_t task_count() const noexcept
            {
                return inputs_.size();
            }

            /**
             * @brief Writes a pattern that starts at @p base to every input and clears the outputs.
             *
             * The inputs are bumped, so a worker pulls them again instead of using its cache.
             */
            void refill(float base)
            {
                for (std::size_t task = 0; task < inputs_.size(); ++task)
                {
                    float* input = inputs_[task].data();
                    float* output = outputs_[task].data();
                    for (std::size_t index = 0; index < inputs_[task].count(); ++index)
                    {
                        input[index] = base + static_cast<float>(index % K_PATTERN_PERIOD);
                        output[index] = -1.0f;
                    }
                    rig_.cluster().bump_buffer(input);
                }
            }

            /** @brief Builds and compiles a graph of one registered task per buffer pair. */
            [[nodiscard]] Internal::Graph::CompiledGraph compile() const
            {
                Internal::Graph::TaskGraph graph(rig_.master().offload());
                Internal::Graph::TaskMetadata metadata;
                metadata.name = "benchmark_increment";
                metadata.op_id = K_INCREMENT;

                for (std::size_t task = 0; task < inputs_.size(); ++task)
                {
                    const BufferView input = inputs_[task].view();
                    const BufferView output = outputs_[task].view();
                    add_registered_task(graph, rig_.kernels(), metadata,
                                        span<const BufferView>(&input, 1),
                                        span<const BufferView>(&output, 1));
                }
                return graph.compile().value();
            }

            /** @brief Returns whether every output holds its input's pattern plus one. */
            [[nodiscard]] bool holds_results(float base) const
            {
                for (const SharedFloats& output : outputs_)
                {
                    const float* values = output.data();
                    for (std::size_t index = 0; index < output.count(); ++index)
                    {
                        const float input = base + static_cast<float>(index % K_PATTERN_PERIOD);
                        if (values[index] != input + 1.0f)
                            return false;
                    }
                }
                return true;
            }

        private:
            /// @brief The rig whose master owns the buffers and runs the tasks.
            OffloadRig& rig_;

            /// @brief The buffer each task reads.
            std::vector<SharedFloats> inputs_;

            /// @brief The buffer each task writes.
            std::vector<SharedFloats> outputs_;
    };

    /** @brief Holds what the timed runs of one batch measured and whether they were right. */
    struct BatchMeasurement
    {
        RunStatistics milliseconds_per_run; /**< Wall time of one execute() of the batch. */
        bool is_correct = true;             /**< Whether every run wrote the expected outputs. */
        std::uint64_t offloaded = 0;        /**< Dispatches queued to a worker by the timed runs. */
        std::uint64_t redispatched = 0;     /**< Tasks taken back during the timed runs. */
    };

    /**
     * @brief Runs a batch once untimed, then times execute() over the default number of runs.
     *
     * The refill, the graph build and the check of the outputs stay outside the timed region.
     */
    BatchMeasurement measure_batch(OffloadRig& rig, IncrementBatch& batch)
    {
        BatchMeasurement measurement;
        float base = 0.0f;

        const auto run_once = [&]() -> double
        {
            base += 1.0f;
            batch.refill(base);
            Internal::Graph::CompiledGraph compiled = batch.compile();

            const auto begin = std::chrono::steady_clock::now();
            rig.master().execution_engine().execute(compiled).value();
            const auto end = std::chrono::steady_clock::now();

            measurement.is_correct = measurement.is_correct && batch.holds_results(base);
            return std::chrono::duration<double, std::milli>(end - begin).count();
        };

        // The first run pays the first touch of every buffer and the worker's cache allocation.
        run_once();

        const std::uint64_t offloaded_before = rig.cluster().offloaded();
        const std::uint64_t redispatched_before = rig.cluster().redispatched();
        measurement.milliseconds_per_run = SushiRuntime::Test::measure(run_once);
        measurement.offloaded = rig.cluster().offloaded() - offloaded_before;
        measurement.redispatched = rig.cluster().redispatched() - redispatched_before;
        return measurement;
    }

    /** @brief Scales the three figures of a measurement by one factor. */
    RunStatistics scaled(const RunStatistics& statistics, double factor)
    {
        RunStatistics result = statistics;
        result.median *= factor;
        result.minimum *= factor;
        result.maximum *= factor;
        return result;
    }
} // namespace

/** @brief Measures 256 small offloaded tasks on two in-process workers and prints their split. */
TEST(Performance_DistributedOffloadBench, OffloadRoundTripThroughput)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const TransportPair transports = make_in_process_pair();
    OffloadRig rig(transports, 2);
    ASSERT_TRUE(rig.wait_until_ready());

    IncrementBatch batch(rig, K_SMALL_TASK_COUNT, K_SMALL_TASK_FLOATS);
    const BatchMeasurement measurement = measure_batch(rig, batch);

    const double tasks = static_cast<double>(batch.task_count());
    const RunStatistics microseconds_per_task =
        scaled(measurement.milliseconds_per_run, 1000.0 / tasks);
    const std::vector<std::uint64_t> split = rig.tasks_run_per_worker();

    std::printf("\n=== Distributed offload: %zu tasks of %zu floats, two in-process workers "
                "(median of %zu runs) ===\n",
                batch.task_count(), K_SMALL_TASK_FLOATS, SushiRuntime::Test::K_DEFAULT_RUN_COUNT);
    std::printf("  %12s | %12s | %12s\n", "ms/run", "us/task", "tasks/s");
    std::printf("  %12.3f | %12.2f | %12.0f\n", measurement.milliseconds_per_run.median,
                microseconds_per_task.median,
                tasks / (measurement.milliseconds_per_run.median / 1000.0));
    std::printf("  offloaded %llu, taken back %llu in the timed runs; kernels run since start: "
                "worker 0 = %llu, worker 1 = %llu\n",
                static_cast<unsigned long long>(measurement.offloaded),
                static_cast<unsigned long long>(measurement.redispatched),
                static_cast<unsigned long long>(split[0]),
                static_cast<unsigned long long>(split[1]));

    MeasurementLabel label;
    label.benchmark    = "distributed_offload";
    label.case_name    = "in_process_two_workers";
    label.unit         = "us_per_task";
    label.worker_count = std::thread::hardware_concurrency();
    label.problem_size = batch.task_count();
    SushiRuntime::Test::print_measurement(label, microseconds_per_task);

    EXPECT_TRUE(measurement.is_correct) << "an output written back to the master is wrong";
    EXPECT_GT(measurement.milliseconds_per_run.median, 0.0);
    EXPECT_GT(measurement.offloaded, 0u) << "nothing was offloaded, so the path was not measured";
}

/** @brief Measures one offloaded task per buffer size, on the in-process fabric and over TCP. */
TEST(Performance_DistributedOffloadBench, RoundTripByBufferSize)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    const std::size_t sizes_in_bytes[] = {
        std::size_t{64} * 1024,
        std::size_t{4} * 1024 * 1024,
        std::size_t{64} * 1024 * 1024,
    };
    const std::function<TransportPair()> kinds[] = {make_in_process_pair, make_tcp_loopback_pair};
    const double mebibyte = 1024.0 * 1024.0;

    std::printf("\n=== Distributed offload round trip by buffer size: one task, one input and one "
                "output of the size (median of %zu runs) ===\n",
                SushiRuntime::Test::K_DEFAULT_RUN_COUNT);
    std::printf("  %14s | %12s | %14s | %10s\n", "transport", "bytes", "ms/round trip", "MiB/s");

    for (const std::function<TransportPair()>& make_pair : kinds)
    {
        const TransportPair transports = make_pair();
        OffloadRig rig(transports, 1);
        ASSERT_TRUE(rig.wait_until_ready()) << transports.name;

        for (const std::size_t bytes : sizes_in_bytes)
        {
            IncrementBatch batch(rig, 1, bytes / sizeof(float));
            const BatchMeasurement measurement = measure_batch(rig, batch);

            // One round trip moves the input to the worker and the output back.
            const double moved_mebibytes = 2.0 * static_cast<double>(bytes) / mebibyte;
            std::printf("  %14s | %12zu | %14.3f | %10.1f\n", transports.name, bytes,
                        measurement.milliseconds_per_run.median,
                        moved_mebibytes / (measurement.milliseconds_per_run.median / 1000.0));

            const std::string case_name = std::string(transports.name) + "_round_trip";
            MeasurementLabel label;
            label.benchmark    = "distributed_offload";
            label.case_name    = case_name.c_str();
            label.unit         = "ms_per_round_trip";
            label.worker_count = std::thread::hardware_concurrency();
            label.problem_size = bytes;
            SushiRuntime::Test::print_measurement(label, measurement.milliseconds_per_run);

            EXPECT_TRUE(measurement.is_correct)
                << transports.name << ", " << bytes << " bytes: the output is wrong";
            EXPECT_EQ(measurement.offloaded, SushiRuntime::Test::K_DEFAULT_RUN_COUNT)
                << transports.name << ", " << bytes << " bytes: a timed run was not offloaded";
            EXPECT_EQ(measurement.redispatched, 0u)
                << transports.name << ", " << bytes << " bytes: a task was taken back";
        }
    }
}
