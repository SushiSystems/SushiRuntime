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
 * @file test_version.cpp
 * @brief Tests the check that compares the headers' version with the loaded library's.
 * @author Mustafa Garip
 */

#include <string>
#include <gtest/gtest.h>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/version.hpp>

using SushiRuntime::Errc;
using SushiRuntime::SushiException;
using SushiRuntime::Version;

namespace
{
    /** @brief Returns the message of the mismatch between two versions, or "" when they agree. */
    std::string mismatch_message(const Version& headers, const Version& loaded)
    {
        try
        {
            SushiRuntime::Detail::require_same_release(headers, loaded);
        }
        catch (const SushiException& mismatch)
        {
            EXPECT_EQ(mismatch.error().code, Errc::unsupported);
            return mismatch.what();
        }
        return std::string();
    }

    TEST(Unit_Version, TheTestProgramMatchesTheLibraryItLoaded)
    {
        EXPECT_TRUE(SushiRuntime::version_matches());
        EXPECT_NO_THROW(SushiRuntime::require_matching_version());
    }

    TEST(Unit_Version, TheHeaderMacrosAndTheLibraryReportOneVersion)
    {
        const Version loaded = SushiRuntime::runtime_version();

        EXPECT_EQ(loaded.major, SUSHIRUNTIME_VERSION_MAJOR);
        EXPECT_EQ(loaded.minor, SUSHIRUNTIME_VERSION_MINOR);
        EXPECT_EQ(loaded.patch, SUSHIRUNTIME_VERSION_PATCH);
        EXPECT_STREQ(SushiRuntime::runtime_version_string(), SUSHIRUNTIME_VERSION_STRING);
    }

    TEST(Unit_Version, APatchDifferenceIsAMatch)
    {
        EXPECT_EQ(mismatch_message(Version{0, 4, 0}, Version{0, 4, 7}), "");
    }

    TEST(Unit_Version, AnOlderLibraryIsRefusedWithBothVersions)
    {
        const std::string message = mismatch_message(Version{0, 4, 0}, Version{0, 3, 2});

        EXPECT_EQ(message, "SushiRuntime headers are 0.4.0 and the loaded library is 0.3.2; "
                           "an older sushiruntime shared library is earlier on the loader path.");
    }

    TEST(Unit_Version, ANewerLibraryIsRefusedAndCalledNewer)
    {
        const std::string by_minor = mismatch_message(Version{0, 4, 0}, Version{0, 5, 1});
        const std::string by_major = mismatch_message(Version{0, 9, 0}, Version{1, 0, 0});

        EXPECT_NE(by_minor.find("headers are 0.4.0"), std::string::npos) << by_minor;
        EXPECT_NE(by_minor.find("library is 0.5.1; a newer"), std::string::npos) << by_minor;
        EXPECT_NE(by_major.find("library is 1.0.0; a newer"), std::string::npos) << by_major;
    }
} // namespace
