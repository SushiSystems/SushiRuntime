/**************************************************************************/
/* test_profile_gate.cpp                                                  */
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
 * @file test_profile_gate.cpp
 * @brief Tests that the front writes a zone only at or below the level its translation unit compiles.
 * @see tests/foundation/profile/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/collector/collector.hpp>
#include <SushiProfiler/events/trace_event.hpp>

#include <SushiRuntime/profile/profile.hpp>

#include "gate_units.hpp"

namespace
{
    /** @brief Opens the collector before each test and closes and empties it after. */
    class Small_ProfileGate : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                close_and_empty();
                ASSERT_TRUE(SushiProfiler::Collector::instance().open(SushiProfiler::CollectorSettings{}));
            }

            void TearDown() override
            {
                close_and_empty();
            }

            /** @brief Returns the names of the sites this thread wrote since the last drain, in order. */
            static std::vector<std::string> names_written()
            {
                SushiProfiler::DrainBatch batch;
                SushiProfiler::Collector::instance().drain(batch);
                std::vector<std::string> names;
                for (SushiProfiler::ThreadDrain& thread : batch.threads)
                {
                    if (thread.thread_index != SushiProfiler::Collector::thread_index())
                        continue;
                    for (const SushiProfiler::TraceEvent& event : thread.events)
                        names.push_back(SushiProfiler::Collector::instance().sites().info(
                            SushiProfiler::site_of(event)).name);
                }
                return names;
            }

            /** @brief Returns whether a site of this name has been registered with the collector. */
            static bool site_registered(const std::string& name)
            {
                const SushiProfiler::SiteRegistry& sites = SushiProfiler::Collector::instance().sites();
                for (std::size_t id = 1; id <= sites.size(); ++id)
                    if (sites.info(static_cast<SushiProfiler::SiteID>(id)).name == name)
                        return true;
                return false;
            }

        private:
            /** @brief Closes the collector and discards what its rings hold. */
            static void close_and_empty()
            {
                SushiProfiler::Collector::instance().close();
                SushiProfiler::DrainBatch discarded;
                SushiProfiler::Collector::instance().drain(discarded);
            }
    };
} // namespace

/** @brief Tests that a stage zone compiled at level 1 writes one event into an open collector. */
TEST_F(Small_ProfileGate, AZoneAtTheCompiledLevelWritesOneEvent)
{
    ProfileGateTests::record_at_level_one();

    const std::vector<std::string> written = names_written();
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.one.stage"), 1);
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.one.value"), 1);
}

/** @brief Tests that a zone above the compiled level writes nothing and registers no site. */
TEST_F(Small_ProfileGate, AZoneAboveTheCompiledLevelWritesNothing)
{
    ProfileGateTests::record_at_level_one();
    ProfileGateTests::record_at_level_two();

    const std::vector<std::string> written = names_written();
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.one.fine"), 0)
        << "a fine zone is level 2, above a unit compiled at level 1";
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.two.detail"), 0)
        << "a detail zone is level 3, above a unit compiled at level 2";
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.two.fine"), 1);

    // A compiled-out zone never reaches the registry.
    EXPECT_FALSE(site_registered("gate.one.fine"));
    EXPECT_FALSE(site_registered("gate.two.detail"));
    EXPECT_TRUE(site_registered("gate.one.stage"));
}

/** @brief Tests that a unit compiled at level 0 writes nothing at all. */
TEST_F(Small_ProfileGate, AUnitCompiledAtLevelZeroWritesNothing)
{
    ProfileGateTests::record_at_level_zero();

    EXPECT_TRUE(names_written().empty());
}

/** @brief Tests that a unit compiled at level 3 writes a zone of every level. */
TEST_F(Small_ProfileGate, AUnitCompiledAtLevelThreeWritesEveryZone)
{
    ProfileGateTests::record_at_level_three();

    const std::vector<std::string> written = names_written();
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.three.stage"), 1);
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.three.fine"), 1);
    EXPECT_EQ(std::count(written.begin(), written.end(), "gate.three.detail"), 1);
}

/** @brief Tests that a zone writes nothing while the collector is closed, whatever its level. */
TEST_F(Small_ProfileGate, AClosedCollectorWritesNothing)
{
    SushiProfiler::Collector::instance().close();

    ProfileGateTests::record_at_level_three();

    EXPECT_TRUE(names_written().empty());
}

/** @brief Tests that the compiled level of this translation unit is the default the build sets. */
TEST(Small_ProfileGateLevel, ThisUnitCompilesAtTheDefaultLevel)
{
    static_assert(SushiRuntime::Internal::Profile::K_COMPILED_LEVEL >= 0, "a level is never negative");
    EXPECT_EQ(SushiRuntime::Internal::Profile::K_COMPILED_LEVEL, SR_PROFILE_LEVEL);
}
