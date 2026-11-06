/**************************************************************************/
/* test_emit_late_count_resolver.cpp                                      */
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
 * @file test_emit_late_count_resolver.cpp
 * @brief Tests late_count_resolver_node(): the fill receives each count's live value.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/emit_late_count_resolver.hpp>

#include "description_runner.hpp"
#include "support/buffer_contents.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the resolver test, which runs a description on the program's runtime. */
class Medium_EmitLateCountResolver : public TestSupport::RuntimeTest
{
};

/** @brief Tests that a fixed and a host count reach the fill as their live values. */
TEST_F(Medium_EmitLateCountResolver, TheFillReceivesTheLiveCounts)
{
    auto out = runtime().buffer<int>(2).value();
    TestSupport::fill_with(out, -1);
    int* const words = out.data();
    const std::array<LateCount, 2> counts{LateCount::fixed(6),
                                          LateCount::from_host(10, [] { return std::size_t{4}; })};
    const Detail::NodeDescription description = Detail::late_count_resolver_node(
        counts, nullptr,
        [words](const std::array<std::size_t, 2>& live)
        {
            words[0] = static_cast<int>(live[0]);
            words[1] = static_cast<int>(live[1]);
        },
        {}, {BufferRegion(words)}, 1);

    Testing::run_description(runtime(), description);

    EXPECT_EQ(TestSupport::contents(out), (std::vector<int>{6, 4}));
}
