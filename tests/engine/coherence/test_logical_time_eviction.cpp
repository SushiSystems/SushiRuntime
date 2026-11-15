/**************************************************************************/
/* test_logical_time_eviction.cpp                                         */
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
 * @file test_logical_time_eviction.cpp
 * @brief Tests the victim LogicalTimeEviction chooses.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/coherence/logical_time_eviction.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns a candidate read next at position next_read. */
    EvictionCandidate candidate(std::size_t next_read, bool sole_copy = false)
    {
        EvictionCandidate result;
        result.next_read = next_read;
        result.sole_copy = sole_copy;
        result.bytes = 64;
        return result;
    }

    TEST(Small_LogicalTimeEviction, NoCandidateGivesNoVictim)
    {
        const LogicalTimeEviction subject;
        EXPECT_FALSE(subject.choose({}).has_value());
    }

    TEST(Small_LogicalTimeEviction, TheInstanceReadFurthestAheadGoesFirst)
    {
        const LogicalTimeEviction subject;
        const std::vector<EvictionCandidate> candidates{candidate(3), candidate(9), candidate(5)};
        EXPECT_EQ(subject.choose(candidates), std::optional<std::size_t>(1));
    }

    TEST(Small_LogicalTimeEviction, AnInstanceNoNodeReadsAgainGoesBeforeAnyOther)
    {
        const LogicalTimeEviction subject;
        const std::vector<EvictionCandidate> candidates{candidate(100), candidate(EvictionCandidate::K_NEVER),
                                                        candidate(7)};
        EXPECT_EQ(subject.choose(candidates), std::optional<std::size_t>(1));
    }

    TEST(Small_LogicalTimeEviction, OfEqualNextReadsTheInstanceThatNeedsNoWriteBackGoesFirst)
    {
        const LogicalTimeEviction subject;
        const std::vector<EvictionCandidate> candidates{candidate(6, true), candidate(6, false),
                                                        candidate(6, false)};
        EXPECT_EQ(subject.choose(candidates), std::optional<std::size_t>(1));
    }

    TEST(Small_LogicalTimeEviction, OfFullyEqualCandidatesTheFirstWins)
    {
        const LogicalTimeEviction subject;
        const std::vector<EvictionCandidate> candidates{candidate(6), candidate(6)};
        EXPECT_EQ(subject.choose(candidates), std::optional<std::size_t>(0));
    }
}
