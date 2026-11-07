/**************************************************************************/
/* test_recorder_parameter_limit.cpp                                      */
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
 * @file test_recorder_parameter_limit.cpp
 * @brief Tests that a rule that names an operation keeps eight parameters and is refused nine.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "recorder_buffers.hpp"
#include "recorder_fixtures.hpp"

/** @brief The entry points that take the parameters of a named rule, as the rows of one test. */
enum class ParameterEntry
{
    Add,     /**< Graph::add(). */
    AddRule  /**< Graph::add_rule(). */
};

/** @brief Fixture of the rows over the entry points that take parameters. */
class Small_RecorderParameterLimit : public RecorderHostTest,
                                     public ::testing::WithParamInterface<ParameterEntry>
{
};

using namespace SushiRuntime;

/** @brief Tests that a rule that names an operation is refused more parameters than the limit. */
namespace RecorderParameterLimitTests
{
    namespace
    {
        using RecorderSupport::K_WIDTH;
        using RecorderSupport::zeroed;

        /**
         * @brief Returns the refusal a call leaves on a recorder, or nothing when it leaves none.
         * @param recorder Cleared before the call and after the refusal is read.
         */
        template <class Call>
        std::optional<Error> refusal_of(Recorder& recorder, Call&& call)
        {
            recorder.clear();
            call();
            std::optional<Error> refused;
            if (recorder.refusal() != nullptr)
                refused = *recorder.refusal();
            recorder.clear();
            return refused;
        }
    } // namespace

    /** @brief Tests that a rule that names an operation is refused nine parameters and records nothing. */
    TEST_P(Small_RecorderParameterLimit, NineParametersAreRefusedAndRecordNothing)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto field = rt.state<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* words = values.data();
        const auto local = [words](std::size_t i) { words[i] += 1u; };
        const auto rule = [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
        {
            next[i] = current[i];
        };
        auto graph = rt.graph();

        const std::uint64_t nine[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        NodeOptions options;
        options.operation = "recorder.limit"_op;
        options.parameters = nine;

        const auto refusal = refusal_of(graph.recorder(), [&]
        {
            if (GetParam() == ParameterEntry::Add)
                graph.add(K_WIDTH, Reads(values), Writes(values), local, options);
            else
                graph.add_rule(field, rule, options);
        });

        ASSERT_TRUE(refusal);
        EXPECT_EQ(refusal->code, ErrorCode::InvalidGraph);
        EXPECT_NE(refusal->message.find("9 parameters"), std::string::npos) << refusal->message;
        EXPECT_NE(refusal->message.find("at most 8"), std::string::npos) << refusal->message;
        EXPECT_EQ(graph.size(), 0u) << "a refused rule must not be recorded";
    }

    /** @brief Tests that a rule that names an operation keeps eight parameters. */
    TEST_P(Small_RecorderParameterLimit, EightParametersAreKept)
    {
        const Runtime& rt = runtime();
        auto values = zeroed(rt);
        auto field = rt.state<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* words = values.data();
        const auto local = [words](std::size_t i) { words[i] += 1u; };
        const auto rule = [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
        {
            next[i] = current[i];
        };
        auto graph = rt.graph();

        const std::uint64_t eight[] = {1, 2, 3, 4, 5, 6, 7, 8};
        NodeOptions options;
        options.operation = "recorder.limit"_op;
        options.parameters = eight;

        if (GetParam() == ParameterEntry::Add)
            graph.add(K_WIDTH, Reads(values), Writes(values), local, options);
        else
            graph.add_rule(field, rule, options);

        EXPECT_EQ(graph.size(), 1u);
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_RecorderParameterLimit,
                             ::testing::Values(ParameterEntry::Add, ParameterEntry::AddRule));
} // namespace RecorderParameterLimitTests
