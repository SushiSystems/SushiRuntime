/**************************************************************************/
/* test_version.cpp                                                       */
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
 * @file test_version.cpp
 * @brief Tests the check that compares the headers' version with the loaded library's.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/version.hpp>

using SushiRuntime::ErrorCode;
using SushiRuntime::Version;

namespace
{
    /** @brief Holds two versions and whether version_matches() takes them for one release. */
    struct MatchRow
    {
        Version headers;  /**< The version the headers declare. */
        Version loaded;   /**< The version the library reports. */
        bool matches;     /**< Whether they agree by major and minor. */
    };

    /** @brief Returns the rows: a patch may differ, a minor or a major may not. */
    std::vector<MatchRow> match_rows()
    {
        return {{Version{0, 4, 0}, Version{0, 4, 0}, true},
                {Version{0, 4, 0}, Version{0, 4, 7}, true},
                {Version{0, 4, 0}, Version{0, 5, 0}, false},
                {Version{0, 4, 0}, Version{0, 3, 9}, false},
                {Version{0, 9, 0}, Version{1, 9, 0}, false}};
    }

    /** @brief Runs version_matches() over the rows of match_rows(). */
    class Small_VersionMatch : public ::testing::TestWithParam<MatchRow>
    {
    };

    /** @brief Tests that a patch difference is no mismatch and a minor or major one is. */
    TEST_P(Small_VersionMatch, MajorAndMinorDecideAndThePatchDoesNot)
    {
        EXPECT_EQ(SushiRuntime::version_matches(GetParam().headers, GetParam().loaded),
                  GetParam().matches);
    }

    /** @brief Runs the match test over every row. */
    INSTANTIATE_TEST_SUITE_P(Versions, Small_VersionMatch, ::testing::ValuesIn(match_rows()));

    TEST(Medium_Version, TheTestProgramMatchesTheLibraryItLoaded)
    {
        EXPECT_TRUE(SushiRuntime::version_matches());
        SushiRuntime::require_matching_version();
    }

    TEST(Medium_Version, TheHeaderMacrosAndTheLibraryReportOneVersion)
    {
        const Version loaded = SushiRuntime::runtime_version();

        EXPECT_EQ(loaded.major, SUSHIRUNTIME_VERSION_MAJOR);
        EXPECT_EQ(loaded.minor, SUSHIRUNTIME_VERSION_MINOR);
        EXPECT_EQ(loaded.patch, SUSHIRUNTIME_VERSION_PATCH);
        EXPECT_STREQ(SushiRuntime::runtime_version_string(), SUSHIRUNTIME_VERSION_STRING);
    }

    TEST(Medium_VersionDeathTest, APatchDifferenceDoesNotStop)
    {
        EXPECT_EXIT(
            {
                SushiRuntime::Detail::require_same_release(Version{0, 4, 0}, Version{0, 4, 7});
                std::exit(0);
            },
            ::testing::ExitedWithCode(0), "");
    }

    TEST(Medium_VersionDeathTest, AnOlderLibraryStopsWithBothVersions)
    {
        EXPECT_DEATH(
            SushiRuntime::Detail::require_same_release(Version{0, 4, 0}, Version{0, 3, 2}),
            "SushiRuntime headers are 0\\.4\\.0 and the loaded library is 0\\.3\\.2; an older "
            "sushiruntime shared library is earlier on the loader path");
    }

    TEST(Medium_VersionDeathTest, ANewerLibraryStopsAndIsCalledNewer)
    {
        EXPECT_DEATH(
            SushiRuntime::Detail::require_same_release(Version{0, 4, 0}, Version{0, 5, 1}),
            "headers are 0\\.4\\.0 and the loaded library is 0\\.5\\.1; a newer");
        EXPECT_DEATH(
            SushiRuntime::Detail::require_same_release(Version{0, 9, 0}, Version{1, 0, 0}),
            "library is 1\\.0\\.0; a newer");
    }
} // namespace
