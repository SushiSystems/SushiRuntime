/**************************************************************************/
/* test_graph_partition.cpp                                               */
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
 * @file test_graph_partition.cpp
 * @brief Tests the partition of a whole dynamic graph, a split run and the profile fold.
 * @see tests/surface/api/graph/plumbing/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

/** @brief Tests the partition of a whole dynamic graph on a runtime created with a profile file. */
namespace GraphPartitionTests
{
    namespace
    {
        /** @brief Elements of the input buffer. */
        constexpr std::size_t K_INPUT = 1024;

        /** @brief Elements of the buffers the nodes write. */
        constexpr std::size_t K_OUTPUT = 16384;

        /** @brief Steps run; two horizons of the coherence history, so at least one fold happens. */
        constexpr std::size_t K_STEPS = 128;

        /** @brief The kind the nodes state, so the folded throughput has a line of its own. */
        constexpr const char K_KIND_NAME[] = "graph_partition_test";

        /** @brief Returns the kind the nodes state. */
        OperationKind test_kind()
        {
            return OperationKind::named(K_KIND_NAME, sizeof(K_KIND_NAME) - 1);
        }

        /** @brief Returns the input values. */
        std::vector<std::uint32_t> input_values()
        {
            std::vector<std::uint32_t> values(K_INPUT);
            for (std::size_t index = 0; index < K_INPUT; ++index)
                values[index] = static_cast<std::uint32_t>(index * 2654435761u);
            return values;
        }

        /** @brief Returns the bytes the producing node leaves, computed on the host. */
        std::vector<std::uint32_t> host_reference()
        {
            const std::vector<std::uint32_t> input = input_values();
            std::vector<std::uint32_t> expected(K_OUTPUT);
            for (std::size_t index = 0; index < K_OUTPUT; ++index)
                expected[index] = input[index % K_INPUT] + static_cast<std::uint32_t>(index);
            return expected;
        }

        /** @brief Returns the text of the file at path; empty when it cannot be read. */
        std::string read_text(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            std::ostringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /** @brief Rewrites every capacity of the profile file at path to capacity, with no reserve. */
        void set_capacities(const std::filesystem::path& path, std::uint64_t capacity)
        {
            std::istringstream lines(read_text(path));
            std::ostringstream rewritten;
            std::string line;
            while (std::getline(lines, line))
            {
                if (line.rfind("capacity_bytes ", 0) == 0)
                    line = "capacity_bytes " + std::to_string(capacity);
                else if (line.rfind("reserve_bytes ", 0) == 0)
                    line = "reserve_bytes 0";
                rewritten << line << '\n';
            }
            std::ofstream(path, std::ios::binary | std::ios::trunc) << rewritten.str();
        }

        /**
         * @brief Multiplies every throughput and memory bandwidth of the profile file at path by factor.
         *
         * A large factor makes every device model far faster than it runs, so the first horizon that
         * measures a device replans.
         */
        void scale_rates(const std::filesystem::path& path, double factor)
        {
            std::istringstream lines(read_text(path));
            std::ostringstream rewritten;
            rewritten.imbue(std::locale::classic());
            rewritten.precision(17);
            std::string line;
            while (std::getline(lines, line))
            {
                std::istringstream words(line);
                words.imbue(std::locale::classic());
                std::string key;
                words >> key;
                if (key == "base_throughput" || key == "memory_bandwidth")
                {
                    double value = 0.0;
                    words >> value;
                    rewritten << key << ' ' << value * factor << '\n';
                }
                else if (key == "throughput")
                {
                    std::string kind;
                    double value = 0.0;
                    words >> kind >> value;
                    rewritten << key << ' ' << kind << ' ' << value * factor << '\n';
                }
                else
                {
                    rewritten << line << '\n';
                }
            }
            std::ofstream(path, std::ios::binary | std::ios::trunc) << rewritten.str();
        }

        /** @brief Returns the lines of the placement journal at path that name a decision: entry, problem and answer. */
        std::vector<std::string> journal_decisions(const std::filesystem::path& path)
        {
            std::istringstream lines(read_text(path));
            std::vector<std::string> decisions;
            std::string line;
            while (std::getline(lines, line))
                if (line.rfind("entry ", 0) == 0 || line.rfind("problem_hash ", 0) == 0 ||
                    line.rfind("assignment_hash ", 0) == 0)
                    decisions.push_back(line);
            return decisions;
        }

        /** @brief Returns how many decisions name reason, such as "first" or "replan". */
        std::size_t count_reason(const std::vector<std::string>& decisions, const std::string& reason)
        {
            std::size_t count = 0;
            for (const std::string& line : decisions)
                if (line.rfind("entry ", 0) == 0 && line.find(" " + reason + " ") != std::string::npos)
                    ++count;
            return count;
        }

        /** @brief Returns the index of the first device of kind CPU, or nothing. */
        std::optional<std::size_t> cpu_device(const std::vector<DeviceDescription>& devices)
        {
            for (std::size_t index = 0; index < devices.size(); ++index)
                if (devices[index].kind == DeviceKind::CPU)
                    return index;
            return std::nullopt;
        }

        /** @brief Returns whether devices hold a device with memory of its own, a pool apart from the CPU's. */
        bool has_device_with_own_memory(const std::vector<DeviceDescription>& devices)
        {
            for (const DeviceDescription& device : devices)
                if (device.kind != DeviceKind::CPU && !device.integrated)
                    return true;
            return false;
        }

        /** @brief Returns the cost the nodes state: their elements, read and written, of test_kind(). */
        NodeOptions costed()
        {
            NodeOptions options;
            options.cost = NodeCost{K_OUTPUT, K_OUTPUT * sizeof(std::uint32_t), K_OUTPUT * sizeof(std::uint32_t),
                                    test_kind(), NodeDType::Unstated};
            return options;
        }
    } // namespace

    /** @brief Fixture that owns a profile file in a directory of its own, removed after the test. */
    class Medium_GraphPartition : public ::testing::Test
    {
        protected:
            /** @brief Makes the directory and names the profile file in it. */
            void SetUp() override
            {
                const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
                directory_ = std::filesystem::temp_directory_path() /
                             (std::string("sushiruntime_graph_partition_") + info->name());
                std::filesystem::remove_all(directory_);
                std::filesystem::create_directories(directory_);
                profile_ = directory_ / "device_profile.txt";
            }

            /** @brief Removes the directory and the profile file. */
            void TearDown() override
            {
                std::error_code ignored;
                std::filesystem::remove_all(directory_, ignored);
            }

            /** @brief Returns a runtime that reads, or measures and writes, the profile file. */
            Runtime create_runtime() const
            {
                return create_runtime(profile_);
            }

            /** @brief Returns a runtime that reads, or measures and writes, the profile file at path. */
            static Runtime create_runtime(const std::filesystem::path& path)
            {
                RuntimeConfig config;
                config.device_profile_file = path.string();
                return Runtime::create(config).value();
            }

            /** @brief Returns the placement journal a runtime over the profile file at path writes. */
            static std::filesystem::path journal_of(const std::filesystem::path& path)
            {
                return std::filesystem::path(path.string() + ".journal");
            }

            /** @brief Returns the reason the machine cannot run the test, or nothing when it can. */
            static std::optional<std::string> missing(const Runtime& runtime, bool own_memory)
            {
                const std::vector<DeviceDescription>& devices = runtime.advanced().devices();
                if (devices.size() < 2)
                    return std::string("the runtime drives one device; a partition needs a CPU and a second device");
                if (!cpu_device(devices))
                    return std::string("the runtime drives no CPU device; the test pairs a CPU with a second device");
                if (own_memory && !has_device_with_own_memory(devices))
                    return std::string("no device has memory of its own, so every device shares the CPU's pool and "
                                       "a split cannot fit where the whole node does not");
                return std::nullopt;
            }

            /// @brief The test's own directory.
            std::filesystem::path directory_;

            /// @brief The profile file in it.
            std::filesystem::path profile_;
    };

    /**
     * @brief Tests that a two-region graph over planned buffers is partitioned as one graph, runs,
     *        reports real rows and, after a horizon, leaves the measured throughput in the profile file.
     */
    TEST_F(Medium_GraphPartition, ARuntimeWithAProfileFilePartitionsRunsReportsAndFoldsTheWholeGraph)
    {
        const Runtime rt = create_runtime();
        if (const std::optional<std::string> reason = missing(rt, false))
            GTEST_SKIP() << *reason;
        const std::string measured_at_create = read_text(profile_);
        ASSERT_FALSE(measured_at_create.empty());

        auto input = rt.buffer<std::uint32_t>(K_INPUT, Residency::Planned).value();
        auto middle = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
        auto output = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
        ASSERT_TRUE(input.write_range({0, K_INPUT}, input_values()));
        auto world = rt.dynamic_graph();
        world.region(0).add(K_OUTPUT, In(input), Out(middle),
                            [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                            { target[index] = source[index % K_INPUT] + static_cast<std::uint32_t>(index); },
                            costed());
        world.region(1).add(K_OUTPUT, In(middle), Out(output),
                            [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                            { target[index] = source[index]; },
                            costed());

        ASSERT_TRUE(world.run(K_STEPS));

        const PartitionReport report = rt.partition_report();
        ASSERT_FALSE(report.refused) << report.refusal;
        ASSERT_EQ(report.rows.size(), rt.advanced().devices().size());
        std::size_t nodes = 0;
        for (const PartitionDeviceRow& row : report.rows)
            nodes += row.nodes;
        // One problem holds both regions, so the report counts the nodes of both.
        EXPECT_EQ(nodes, 2u);
        EXPECT_GT(report.step_seconds, 0.0);
        EXPECT_GT(report.measured_step_seconds, 0.0);
        EXPECT_EQ(output.read_range({0, K_OUTPUT}).value(), host_reference());

        const std::string folded = read_text(profile_);
        EXPECT_NE(folded, measured_at_create);
        EXPECT_NE(folded.find("throughput " + std::to_string(test_kind().value) + " "), std::string::npos)
            << folded;
    }

    /**
     * @brief Tests that a node no pool holds whole is cut, runs its parts in a real step and leaves
     *        the bytes a host run computes.
     */
    TEST_F(Medium_GraphPartition, ANodeNoPoolHoldsWholeRunsSplitAndLeavesTheBytesOfAHostRun)
    {
        {
            // The first runtime measures the machine and writes the profile file.
            const Runtime measuring = create_runtime();
            if (const std::optional<std::string> reason = missing(measuring, true))
                GTEST_SKIP() << *reason;
        }
        // Each pool now holds the input and all but one element of the output, never both whole.
        set_capacities(profile_, (K_INPUT + K_OUTPUT - 1) * sizeof(std::uint32_t));
        const Runtime rt = create_runtime();

        auto input = rt.buffer<std::uint32_t>(K_INPUT, Residency::Planned).value();
        auto output = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
        ASSERT_TRUE(input.write_range({0, K_INPUT}, input_values()));

        auto split = std::make_shared<NodeSplit>();
        split->axis = 0;
        split->extent = K_OUTPUT;
        const GlobalHandle source_handle = input.handle();
        const GlobalHandle target_handle = output.handle();
        split->regions = [source_handle, target_handle](const SplitPart& part)
        {
            constexpr std::size_t K_WORD = sizeof(std::uint32_t);
            SplitRegions regions;
            regions.reads.push_back(BufferRegion(source_handle, 0, K_INPUT * K_WORD));
            regions.writes.push_back(BufferRegion(target_handle, static_cast<std::size_t>(part.begin) * K_WORD,
                                                  static_cast<std::size_t>(part.end - part.begin) * K_WORD));
            return regions;
        };
        auto slices = std::make_shared<std::atomic<std::size_t>>(0);
        split->slice = [slices](const SplitPart& part, sycl::queue& queue, span<void* const> pointers)
        {
            slices->fetch_add(1);
            const std::uint32_t* source = static_cast<const std::uint32_t*>(pointers[0]);
            std::uint32_t* target = static_cast<std::uint32_t*>(pointers[1]);
            const std::size_t begin = static_cast<std::size_t>(part.begin);
            queue.parallel_for(sycl::range<1>(static_cast<std::size_t>(part.end - part.begin)),
                               [=](sycl::id<1> id)
                               {
                                   const std::size_t index = begin + id[0];
                                   target[index] = source[index % K_INPUT] + static_cast<std::uint32_t>(index);
                               });
        };
        NodeOptions options = costed();
        options.split = split;

        auto world = rt.dynamic_graph();
        world.region(0).add(K_OUTPUT, In(input), Out(output),
                            [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                            { target[index] = source[index % K_INPUT] + static_cast<std::uint32_t>(index); },
                            options);

        ASSERT_TRUE(world.run());

        const PartitionReport report = rt.partition_report();
        ASSERT_FALSE(report.refused) << report.refusal;
        // The node ran as parts, one slice per part, not as its whole kernel.
        EXPECT_GE(slices->load(), 2u) << report.to_text();
        EXPECT_EQ(output.read_range({0, K_OUTPUT}).value(), host_reference());
    }

    /**
     * @brief Tests that a second runtime given the placement journal of a run that rebalanced puts the
     *        same plans in force and leaves the same bytes.
     */
    TEST_F(Medium_GraphPartition, ARuntimeReplayingAJournalPlacesAsTheJournalledRunAndLeavesItsBytes)
    {
        {
            const Runtime measuring = create_runtime();
            if (const std::optional<std::string> reason = missing(measuring, false))
                GTEST_SKIP() << *reason;
        }
        scale_rates(profile_, 1.0e6);
        const std::filesystem::path replay_profile = directory_ / "replay_profile.txt";
        std::filesystem::copy_file(profile_, replay_profile);

        // Runs the two-region graph on rt for two runs of K_STEPS and returns the output bytes.
        const auto run_graph = [](const Runtime& rt)
        {
            auto input = rt.buffer<std::uint32_t>(K_INPUT, Residency::Planned).value();
            auto middle = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
            auto output = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
            EXPECT_TRUE(input.write_range({0, K_INPUT}, input_values()));
            auto world = rt.dynamic_graph();
            world.region(0).add(K_OUTPUT, In(input), Out(middle),
                                [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                                { target[index] = source[index % K_INPUT] + static_cast<std::uint32_t>(index); },
                                costed());
            world.region(1).add(K_OUTPUT, In(middle), Out(output),
                                [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                                { target[index] = source[index]; },
                                costed());
            // The second run commits the plan the first run's horizons put in force.
            EXPECT_TRUE(world.run(K_STEPS));
            EXPECT_TRUE(world.run(K_STEPS));
            return output.read_range({0, K_OUTPUT}).value();
        };

        std::vector<std::uint32_t> journalled_bytes;
        {
            const Runtime rt = create_runtime();
            journalled_bytes = run_graph(rt);
        }
        const std::vector<std::string> journalled = journal_decisions(journal_of(profile_));
        ASSERT_GE(count_reason(journalled, "first"), 1u) << read_text(journal_of(profile_));
        // The doctored profile models every device a million times faster than it runs.
        ASSERT_GE(journalled.size(), 6u) << "no horizon rebalanced: " << read_text(journal_of(profile_));

        std::vector<std::uint32_t> replayed_bytes;
        {
            const Runtime rt = create_runtime(replay_profile);
            ASSERT_TRUE(rt.advanced().replay_placements(journal_of(profile_).string()));
            replayed_bytes = run_graph(rt);
        }

        EXPECT_EQ(journal_decisions(journal_of(replay_profile)), journalled);
        EXPECT_EQ(replayed_bytes, journalled_bytes);
        EXPECT_EQ(replayed_bytes, host_reference());
    }

    /**
     * @brief Tests that a static graph keeps its partition across builds and, after a horizon
     *        rebalanced it, builds again from the plan put in force rather than partitioning anew.
     */
    TEST_F(Medium_GraphPartition, AStaticGraphRebuildsFromThePlanAHorizonPutInForce)
    {
        {
            const Runtime measuring = create_runtime();
            if (const std::optional<std::string> reason = missing(measuring, false))
                GTEST_SKIP() << *reason;
        }
        scale_rates(profile_, 1.0e6);
        const Runtime rt = create_runtime();

        auto input = rt.buffer<std::uint32_t>(K_INPUT, Residency::Planned).value();
        auto output = rt.buffer<std::uint32_t>(K_OUTPUT, Residency::Planned).value();
        ASSERT_TRUE(input.write_range({0, K_INPUT}, input_values()));
        auto graph = rt.graph();
        graph.add(K_OUTPUT, In(input), Out(output),
                  [](std::size_t index, const std::uint32_t* source, std::uint32_t* target)
                  { target[index] = source[index % K_INPUT] + static_cast<std::uint32_t>(index); },
                  costed());

        ASSERT_TRUE(graph.run(K_STEPS));
        ASSERT_EQ(graph.compile_count(), 1u);
        const std::vector<std::string> after_horizons = journal_decisions(journal_of(profile_));
        ASSERT_GE(after_horizons.size(), 6u) << "no horizon rebalanced: " << read_text(journal_of(profile_));

        ASSERT_TRUE(graph.run());

        // The rebalanced plan made the graph stale, and the build took the plan in force.
        EXPECT_EQ(graph.compile_count(), 2u);
        EXPECT_EQ(count_reason(journal_decisions(journal_of(profile_)), "first"), 1u)
            << read_text(journal_of(profile_));
        EXPECT_FALSE(rt.partition_report().refused) << rt.partition_report().refusal;
        EXPECT_EQ(output.read_range({0, K_OUTPUT}).value(), host_reference());
    }
} // namespace GraphPartitionTests
