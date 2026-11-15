/**************************************************************************/
/* test_splitting.cpp                                                     */
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
 * @file test_splitting.cpp
 * @brief Tests that ListSchedulingPartitioner cuts a node that declares a split.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/coherence/assignment_hash.hpp>
#include <SushiRuntime/coherence/list_scheduling_partitioner.hpp>
#include <SushiRuntime/task/split_dispatch.hpp>

#include "../partition_problem_builder.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /**
     * @brief Returns one node that reads a 100-byte buffer and writes a 1200-byte one, more than a
     *        pool's 900 usable bytes, over two devices; it declares a split of extent when extent
     *        is not zero.
     */
    PartitionProblem wide_node(std::uint64_t extent)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.cost.operations = 1200;
        node.reads.push_back(add_buffer(problem, 100));
        node.writes.push_back(add_buffer(problem, 1200));
        if (extent != 0)
            node.split = PartitionSplit{0, extent};
        problem.nodes.push_back(node);
        return problem;
    }

    /** @brief Returns a queue on a CPU device, or nothing when the machine offers none. */
    std::optional<sycl::queue> cpu_queue()
    {
        try
        {
            return sycl::queue(sycl::cpu_selector_v);
        }
        catch (const sycl::exception&)
        {
            return std::nullopt;
        }
    }

    /** @brief Tests that a node that fits no pool whole is cut by device speed, one part per device. */
    TEST(Small_ListSchedulingPartitioner, ANodeThatFitsNoPoolWholeRunsSplit)
    {
        const PartitionProblem problem = wide_node(12);

        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result.value().splits.size(), 1u);
        const PlannedSplit& split = result.value().splits[0];
        EXPECT_EQ(split.node, 0u);
        ASSERT_EQ(split.parts.size(), 2u);
        EXPECT_EQ(split.parts[0].device, host_key(1));
        EXPECT_EQ(split.parts[0].part, (SplitPart{0, 0, 8, 0, 2}));
        EXPECT_EQ(split.parts[1].device, host_key(2));
        EXPECT_EQ(split.parts[1].part, (SplitPart{0, 8, 12, 1, 2}));
        EXPECT_EQ(split.parts[0].writes[0].end, 800u);
        EXPECT_EQ(split.parts[1].writes[0].begin, 800u);
        EXPECT_EQ(split.parts[1].writes[0].end, 1200u);
        ASSERT_EQ(result.value().node_devices.size(), 1u);
        EXPECT_EQ(result.value().node_devices[0], host_key(1));
    }

    /** @brief Tests that with splitting turned off the node stays one part, and so is refused. */
    TEST(Small_ListSchedulingPartitioner, SplittingTurnedOffGivesOnePart)
    {
        const PartitionProblem problem = wide_node(12);

        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}),
                                                    ListSchedulingPartitioner::Splitting::Off);
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().error.code, ErrorCode::AllocationFailed);
    }

    /** @brief Tests that with splitting turned off a node that fits whole has no parts. */
    TEST(Small_ListSchedulingPartitioner, SplittingTurnedOffLeavesAFittingNodeWhole)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.cost.operations = 100000;
        node.writes.push_back(add_buffer(problem, 10));
        node.split = PartitionSplit{0, 100};
        problem.nodes.push_back(node);

        const ListSchedulingPartitioner partitioner(speed_model({1.0, 1.0}),
                                                    ListSchedulingPartitioner::Splitting::Off);
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);

        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result.value().splits.empty());
        EXPECT_EQ(result.value().node_devices.size(), 1u);
    }

    /** @brief Tests that a node with no declared split that fits no pool whole is still refused. */
    TEST(Small_ListSchedulingPartitioner, ANodeWithNoDeclaredSplitIsNotCut)
    {
        const PartitionProblem problem = wide_node(0);

        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));

        EXPECT_FALSE(partitioner.partition(problem).has_value());
    }

    /** @brief Tests that a split plan hashes equal across runs. */
    TEST(Small_ListSchedulingPartitioner, ASplitPlanIsDeterministic)
    {
        const PartitionProblem problem = wide_node(12);
        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));

        const Result<PartitionAssignment, PartitionRefusal> first = partitioner.partition(problem);
        const Result<PartitionAssignment, PartitionRefusal> second = partitioner.partition(problem);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(assignment_hash(first.value()), assignment_hash(second.value()));
    }

    /** @brief Tests that the parts of a node that fits no pool whole, run each on its device, give the bytes of an unsplit host run. */
    TEST(Small_ListSchedulingPartitioner, ASplitRunGivesTheBytesOfAnUnsplitHostRun)
    {
        std::optional<sycl::queue> queue = cpu_queue();
        if (!queue)
            GTEST_SKIP() << "No CPU device.";
        constexpr std::uint64_t K_EXTENT = 12;
        const PartitionProblem problem = wide_node(K_EXTENT);
        const ListSchedulingPartitioner partitioner(speed_model({2.0, 1.0}));
        const Result<PartitionAssignment, PartitionRefusal> result = partitioner.partition(problem);
        ASSERT_TRUE(result.has_value());
        ASSERT_EQ(result.value().splits.size(), 1u);

        std::vector<std::uint32_t> input(K_EXTENT);
        for (std::uint64_t index = 0; index < K_EXTENT; ++index)
            input[index] = static_cast<std::uint32_t>(index * 2654435761u);
        std::vector<std::uint32_t> output(K_EXTENT, 0);
        NodeSplit split;
        split.extent = K_EXTENT;
        split.slice = [&](const SplitPart& part, sycl::queue&, span<void* const>)
        {
            for (std::uint64_t index = part.begin; index < part.end; ++index)
                output[index] = input[index] * 3u + 1u;
        };

        std::vector<Internal::Graph::SplitDispatchPart> parts;
        for (const PlannedSplitPart& planned : result.value().splits[0].parts)
            parts.push_back(Internal::Graph::SplitDispatchPart{planned.device.ordinal - 1u, planned.part});
        Internal::Graph::dispatch_split(split, span<const Internal::Graph::SplitDispatchPart>(parts.data(), parts.size()),
                                        nullptr, [&](std::size_t) -> sycl::queue& { return *queue; });
        const std::vector<std::uint32_t> split_bytes = output;

        std::fill(output.begin(), output.end(), 0u);
        split.slice(SplitPart{0, 0, K_EXTENT, 0, 1}, *queue, span<void* const>());

        EXPECT_EQ(split_bytes, output);
    }

    /** @brief Inner iterations of the product kernel per element. */
    constexpr std::uint32_t K_PRODUCT_ITERATIONS = 4096;

    /** @brief Elements of the product's axis. */
    constexpr std::uint64_t K_PRODUCT_EXTENT = std::uint64_t{1} << 22;

    /**
     * @brief Holds the queues and output buffers of the product on the first two GPUs, and its
     *        node split: each element folds K_PRODUCT_ITERATIONS multiply-adds into its own word.
     */
    class ProductOnGPUs
    {
        public:
            /** @brief Builds the product on gpus, which hold at least two devices. */
            explicit ProductOnGPUs(const std::vector<sycl::device>& gpus)
            {
                queues_.reserve(2);
                for (std::size_t device = 0; device < 2; ++device)
                {
                    queues_.emplace_back(gpus[device], sycl::property::queue::in_order());
                    outputs_.push_back(sycl::malloc_device<std::uint32_t>(K_PRODUCT_EXTENT, queues_.back()));
                }
                split_.extent = K_PRODUCT_EXTENT;
                split_.slice = [this](const SplitPart& part, sycl::queue& queue, span<void* const>)
                {
                    std::uint32_t* output = outputs_[&queue == &queues_[0] ? 0 : 1];
                    const std::uint64_t begin = part.begin;
                    const sycl::range<1> range(static_cast<std::size_t>(part.end - part.begin));
                    queue.submit([=](sycl::handler& handler)
                    {
                        handler.parallel_for(range, [=](sycl::id<1> id)
                        {
                            const std::uint64_t index = begin + id[0];
                            std::uint32_t value = static_cast<std::uint32_t>(index);
                            for (std::uint32_t step = 0; step < K_PRODUCT_ITERATIONS; ++step)
                                value = value * 1664525u + 1013904223u;
                            output[index] = value;
                        });
                    });
                };
            }

            ProductOnGPUs(const ProductOnGPUs&) = delete;
            ProductOnGPUs& operator=(const ProductOnGPUs&) = delete;

            /** @brief Frees the output buffers. */
            ~ProductOnGPUs()
            {
                for (std::size_t device = 0; device < outputs_.size(); ++device)
                    sycl::free(outputs_[device], queues_[device]);
            }

            /** @brief Runs parts and returns the wall seconds until every queue is done. */
            [[nodiscard]] double run(const std::vector<Internal::Graph::SplitDispatchPart>& parts)
            {
                const auto start = std::chrono::steady_clock::now();
                Internal::Graph::dispatch_split(split_, span<const Internal::Graph::SplitDispatchPart>(parts.data(), parts.size()),
                                                nullptr, [this](std::size_t device) -> sycl::queue& { return queues_[device]; });
                for (sycl::queue& queue : queues_)
                    queue.wait();
                return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            }

            /** @brief Returns the least wall seconds of three runs of parts, after one warm-up run. */
            [[nodiscard]] double best_of_three(const std::vector<Internal::Graph::SplitDispatchPart>& parts)
            {
                (void)run(parts);
                double best = run(parts);
                for (int repeat = 0; repeat < 2; ++repeat)
                    best = std::min(best, run(parts));
                return best;
            }

        private:
            /// @brief One in-order queue per GPU.
            std::vector<sycl::queue> queues_;

            /// @brief One output buffer of the whole extent per GPU.
            std::vector<std::uint32_t*> outputs_;

            /// @brief The product's split.
            NodeSplit split_;
    };

    /** @brief Returns the parts the partitioner gives the product node under splitting. */
    std::vector<Internal::Graph::SplitDispatchPart> planned_parts(const std::shared_ptr<const CostModel>& model,
                                                                  ListSchedulingPartitioner::Splitting splitting)
    {
        PartitionProblem problem;
        add_devices(problem, 2);
        PartitionNode node;
        node.cost.operations = K_PRODUCT_EXTENT * K_PRODUCT_ITERATIONS;
        node.writes.push_back(add_buffer(problem, K_PRODUCT_EXTENT * sizeof(std::uint32_t)));
        node.split = PartitionSplit{0, K_PRODUCT_EXTENT};
        problem.nodes.push_back(node);

        const Result<PartitionAssignment, PartitionRefusal> result = ListSchedulingPartitioner(model, splitting).partition(problem);
        EXPECT_TRUE(result.has_value());
        std::vector<Internal::Graph::SplitDispatchPart> parts;
        if (!result.has_value())
            return parts;
        if (result.value().splits.empty())
        {
            parts.push_back({result.value().node_devices[0].ordinal - 1u, SplitPart{0, 0, K_PRODUCT_EXTENT, 0, 1}});
            return parts;
        }
        for (const PlannedSplitPart& planned : result.value().splits[0].parts)
            parts.push_back({planned.device.ordinal - 1u, planned.part});
        return parts;
    }

    /** @brief Tests on the first machine that the split product's step is shorter than with splitting turned off. */
    TEST(Large_ListSchedulingPartitioner, ASplitProductStepIsShorterThanWithSplittingOff)
    {
        const std::vector<sycl::device> gpus = sycl::device::get_devices(sycl::info::device_type::gpu);
        if (gpus.size() < 2)
            GTEST_SKIP() << "Fewer than two GPUs.";
        ProductOnGPUs product(gpus);

        DeviceProfileSet profiles;
        for (std::uint32_t device = 0; device < 2; ++device)
        {
            const double seconds = product.best_of_three({{device, SplitPart{0, 0, K_PRODUCT_EXTENT, 0, 1}}});
            DeviceProfile profile;
            profile.capacity_bytes = gpus[device].get_info<sycl::info::device::global_mem_size>();
            profile.memory_bandwidth = 1.0e11;
            profile.base_throughput = static_cast<double>(K_PRODUCT_EXTENT * K_PRODUCT_ITERATIONS) / seconds;
            profile.paths["pcie"] = PathProfile{1.0e10, 1.0e-5};
            profiles.set(host_key(device + 1), profile);
        }
        const auto model = std::make_shared<const CostModel>(profiles);

        const std::vector<Internal::Graph::SplitDispatchPart> split_parts =
            planned_parts(model, ListSchedulingPartitioner::Splitting::Allowed);
        const std::vector<Internal::Graph::SplitDispatchPart> whole_parts =
            planned_parts(model, ListSchedulingPartitioner::Splitting::Off);
        ASSERT_EQ(split_parts.size(), 2u);
        ASSERT_EQ(whole_parts.size(), 1u);

        const double split_seconds = product.best_of_three(split_parts);
        const double whole_seconds = product.best_of_three(whole_parts);

        EXPECT_LT(split_seconds, whole_seconds);
    }
} // namespace
