/**************************************************************************/
/* test_worker_options_parser.cpp                                         */
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
 * @file test_worker_options_parser.cpp
 * @brief Tests parse_worker_options(): defaults, flags, the environment, help, version and refusals.
 * @see tests/extension/distributed/README.md
 * @author Mustafa Garip
 */

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>

#include <SushiRuntime/distributed/worker_main.hpp>
#include <SushiRuntime/distributed/worker_options_parser.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Names the variables a parser test pretends the process has. */
    using FakeEnvironment = std::map<std::string, std::string>;

    /** @brief Returns a reader over a table, so no test changes the process's environment. */
    EnvironmentReader reader_of(FakeEnvironment variables)
    {
        return [variables = std::move(variables)](const char* name) -> std::optional<std::string>
        {
            const auto found = variables.find(name);
            if (found == variables.end())
                return std::nullopt;
            return found->second;
        };
    }

    /** @brief Parses a command line, given without the program's name, over a fake environment. */
    WorkerOptionsResult parse(const std::vector<const char*>& arguments,
                              FakeEnvironment variables = FakeEnvironment())
    {
        return parse_worker_options(
            span<const char* const>(arguments.data(), arguments.size()),
            reader_of(std::move(variables)));
    }

    /** @brief Holds one command line the parser must refuse and a part of its message. */
    struct RefusedCase
    {
        std::vector<const char*> arguments; /**< The flags. */
        FakeEnvironment variables;          /**< The environment. */
        std::string expected;               /**< Text the message must hold. */
    };
}

/** @brief Tests that the master alone gives the defaults of the flag table. */
TEST(Medium_WorkerOptionsParserTest, MasterAloneGivesTheDefaults)
{
    const WorkerOptionsResult result = parse({"--master", "node7:7000"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "node7");
    EXPECT_EQ(result.options.master.port, 7000);
    EXPECT_TRUE(result.options.secret_file.empty());
    EXPECT_TRUE(result.options.name.empty());
    EXPECT_EQ(result.options.max_jobs, 1024u);
    EXPECT_EQ(result.options.cache_bytes, 0u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(1000));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(5000));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(60));
    EXPECT_TRUE(result.options.reconnect);
    EXPECT_TRUE(result.options.log_level.empty());
}

/** @brief Tests every flag, half written `--flag value` and half `--flag=value`. */
TEST(Medium_WorkerOptionsParserTest, EveryFlagIsRead)
{
    const WorkerOptionsResult result = parse(
        {"--master", "[::1]:7001", "--secret-file=keys/cluster.key", "--name", "gpu-box",
         "--max-jobs=8", "--cache-bytes", "4096", "--heartbeat-ms=250",
         "--pull-idle-timeout-ms", "0", "--connect-timeout-s=0", "--no-reconnect",
         "--log-level", "debug"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "::1");
    EXPECT_EQ(result.options.master.port, 7001);
    EXPECT_EQ(result.options.secret_file, std::filesystem::path("keys/cluster.key"));
    EXPECT_EQ(result.options.name, "gpu-box");
    EXPECT_EQ(result.options.max_jobs, 8u);
    EXPECT_EQ(result.options.cache_bytes, 4096u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(250));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(0));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(0));
    EXPECT_FALSE(result.options.reconnect);
    EXPECT_EQ(result.options.log_level, "debug");
}

/** @brief Tests that each variable of the table fills the setting no flag gives. */
TEST(Medium_WorkerOptionsParserTest, EnvironmentFillsWhatNoFlagGives)
{
    const WorkerOptionsResult result = parse(
        {}, {{"SR_MASTER", "10.0.0.5:7002"},
             {"SR_CLUSTER_SECRET_FILE", "from_environment.key"},
             {"SR_WORKER_NAME", "named-by-variable"},
             {"SR_WORKER_MAX_JOBS", "16"},
             {"SR_WORKER_CACHE_BYTES", "1048576"},
             {"SR_WORKER_HEARTBEAT_MS", "500"},
             {"SR_WORKER_PULL_IDLE_TIMEOUT_MS", "9000"},
             {"SR_WORKER_CONNECT_TIMEOUT_S", "5"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "10.0.0.5");
    EXPECT_EQ(result.options.master.port, 7002);
    EXPECT_EQ(result.options.secret_file, std::filesystem::path("from_environment.key"));
    EXPECT_EQ(result.options.name, "named-by-variable");
    EXPECT_EQ(result.options.max_jobs, 16u);
    EXPECT_EQ(result.options.cache_bytes, 1048576u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(500));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(9000));
    EXPECT_EQ(result.options.connect_timeout, std::chrono::seconds(5));
    EXPECT_TRUE(result.options.reconnect);
}

/** @brief Tests that a flag beats its variable and that a variable behind a flag is unread. */
TEST(Medium_WorkerOptionsParserTest, FlagBeatsEnvironmentBeatsDefault)
{
    const WorkerOptionsResult result = parse(
        {"--master", "flag-host:1", "--max-jobs", "3"},
        {{"SR_MASTER", "variable-host:2"},
         {"SR_WORKER_MAX_JOBS", "not a number"},
         {"SR_WORKER_HEARTBEAT_MS", "40"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.master.host, "flag-host");
    EXPECT_EQ(result.options.max_jobs, 3u);
    EXPECT_EQ(result.options.heartbeat_interval, std::chrono::milliseconds(40));
    EXPECT_EQ(result.options.pull_idle_timeout, std::chrono::milliseconds(5000));
}

/** @brief Tests that a variable set to nothing leaves the default in place. */
TEST(Medium_WorkerOptionsParserTest, EmptyVariableCountsAsUnset)
{
    const WorkerOptionsResult result =
        parse({"--master", "node7:7000"}, {{"SR_WORKER_MAX_JOBS", ""}, {"SR_WORKER_NAME", ""}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_EQ(result.options.max_jobs, 1024u);
    EXPECT_TRUE(result.options.name.empty());
}

/** @brief Tests that `--help` wins without a master and that its text names every flag. */
TEST(Medium_WorkerOptionsParserTest, HelpListsEveryFlagAndVariable)
{
    const WorkerOptionsResult result = parse({"--help"});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Help);
    for (const char* word :
         {"--master", "--secret-file", "--name", "--max-jobs", "--cache-bytes", "--heartbeat-ms",
          "--pull-idle-timeout-ms", "--connect-timeout-s", "--no-reconnect", "--log-level",
          "--version", "--help", "--label", "SR_WORKER_LABELS", "SR_MASTER", "SR_CLUSTER_SECRET_FILE", "SR_WORKER_NAME",
          "SR_WORKER_MAX_JOBS", "SR_WORKER_CACHE_BYTES", "SR_WORKER_HEARTBEAT_MS",
          "SR_WORKER_PULL_IDLE_TIMEOUT_MS", "SR_WORKER_CONNECT_TIMEOUT_S"})
        EXPECT_NE(result.message.find(word), std::string::npos) << word;
}

/** @brief Tests that `--version` is reported without a master. */
TEST(Medium_WorkerOptionsParserTest, VersionNeedsNoMaster)
{
    EXPECT_EQ(parse({"--version"}).status, WorkerOptionsStatus::Version);
}

/** @brief Tests each command line the parser refuses and what its one line names. */
TEST(Medium_WorkerOptionsParserTest, EachRefusalNamesWhatWasWrong)
{
    const std::vector<RefusedCase> cases = {
        {{}, {}, "no master given"},
        {{"--master"}, {}, "--master needs a value"},
        {{"--master", "node7"}, {}, "--master: "},
        {{"--master", "node7:99999"}, {}, "--master: "},
        {{"--master", "node7:0"}, {}, "port 0"},
        {{}, {{"SR_MASTER", "::1:7000"}}, "SR_MASTER: "},
        {{"--master", "a:1", "--frobnicate"}, {}, "unknown argument '--frobnicate'"},
        {{"--master", "a:1", "stray"}, {}, "unknown argument 'stray'"},
        {{"--master", "a:1", "--no-reconnect=yes"}, {}, "--no-reconnect takes no value"},
        {{"--master", "a:1", "--max-jobs", "0"}, {}, "--max-jobs: '0' is not a whole number"},
        {{"--master", "a:1", "--max-jobs", "4294967296"}, {}, "--max-jobs: "},
        {{"--master", "a:1", "--max-jobs", "12x"}, {}, "--max-jobs: '12x'"},
        {{"--master", "a:1", "--max-jobs", "-1"}, {}, "--max-jobs: '-1'"},
        {{"--master", "a:1", "--max-jobs="}, {}, "--max-jobs: ''"},
        {{"--master", "a:1", "--cache-bytes", "many"}, {}, "--cache-bytes: 'many'"},
        {{"--master", "a:1", "--heartbeat-ms", "0"}, {}, "--heartbeat-ms: '0'"},
        {{"--master", "a:1", "--pull-idle-timeout-ms", "1.5"}, {}, "--pull-idle-timeout-ms: "},
        {{"--master", "a:1", "--connect-timeout-s", "soon"}, {}, "--connect-timeout-s: 'soon'"},
        {{"--master", "a:1"}, {{"SR_WORKER_HEARTBEAT_MS", "fast"}},
         "SR_WORKER_HEARTBEAT_MS: 'fast'"},
    };

    for (const RefusedCase& refused : cases)
    {
        const WorkerOptionsResult result = parse(refused.arguments, refused.variables);

        EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid) << refused.expected;
        EXPECT_NE(result.message.find(refused.expected), std::string::npos)
            << "expected '" << refused.expected << "' in '" << result.message << "'";
        EXPECT_EQ(result.message.find('\n'), std::string::npos) << result.message;
    }
}

/** @brief Tests that a name HELLO cannot carry is refused with both lengths. */
TEST(Medium_WorkerOptionsParserTest, NameAbove255BytesIsRefused)
{
    const std::string name(256, 'n');

    const WorkerOptionsResult result = parse({"--master", "a:1", "--name", name.c_str()});

    EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid);
    EXPECT_NE(result.message.find("256 bytes"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("255"), std::string::npos) << result.message;
}

/** @brief Tests that the variable's labels come first, then the flags', three in all. */
TEST(Medium_WorkerOptionsParserTest, LabelsComeFromTheVariableAndRepeatedFlags)
{
    const WorkerOptionsResult result =
        parse({"--master", "a:1", "--label", "rack=r4", "--label=zone=eu=west"},
              {{"SR_WORKER_LABELS", "role=trainer"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    ASSERT_EQ(result.options.labels.size(), 3u);
    EXPECT_EQ(result.options.labels[0].key, "role");
    EXPECT_EQ(result.options.labels[0].value, "trainer");
    EXPECT_EQ(result.options.labels[1].key, "rack");
    EXPECT_EQ(result.options.labels[1].value, "r4");
    EXPECT_EQ(result.options.labels[2].key, "zone");
    EXPECT_EQ(result.options.labels[2].value, "eu=west");
}

/** @brief Tests that a variable holding several pairs is split at each comma. */
TEST(Medium_WorkerOptionsParserTest, TheVariableHoldsPairsSeparatedByCommas)
{
    const WorkerOptionsResult result =
        parse({"--master", "a:1"}, {{"SR_WORKER_LABELS", "role=trainer,gpu=,rack=r4"}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    ASSERT_EQ(result.options.labels.size(), 3u);
    EXPECT_EQ(result.options.labels[1].key, "gpu");
    EXPECT_TRUE(result.options.labels[1].value.empty());
    EXPECT_EQ(result.options.labels[2].key, "rack");
}

/** @brief Tests that no label means an empty list, and an empty variable counts as unset. */
TEST(Medium_WorkerOptionsParserTest, NoLabelGivesAnEmptyList)
{
    const WorkerOptionsResult result =
        parse({"--master", "a:1"}, {{"SR_WORKER_LABELS", ""}});

    ASSERT_EQ(result.status, WorkerOptionsStatus::Run) << result.message;
    EXPECT_TRUE(result.options.labels.empty());
}

/** @brief Tests the three label refusals, from a flag and from the variable. */
TEST(Medium_WorkerOptionsParserTest, ALabelThatBreaksARuleIsRefused)
{
    const std::vector<RefusedCase> cases = {
        {{"--master", "a:1", "--label", "=value"}, {}, "--label: label '=value' has an empty key"},
        {{"--master", "a:1", "--label", "rack"}, {}, "--label: label 'rack' has no '='"},
        {{"--master", "a:1", "--label", "rack=r4", "--label", "rack=r5"}, {},
         "--label: label key 'rack' is given twice"},
        {{"--master", "a:1"}, {{"SR_WORKER_LABELS", "=value"}},
         "SR_WORKER_LABELS: label '=value' has an empty key"},
        {{"--master", "a:1"}, {{"SR_WORKER_LABELS", "rack"}},
         "SR_WORKER_LABELS: label 'rack' has no '='"},
        {{"--master", "a:1"}, {{"SR_WORKER_LABELS", "rack=r4,rack=r5"}},
         "SR_WORKER_LABELS: label key 'rack' is given twice"},
        {{"--master", "a:1", "--label", "rack=r5"}, {{"SR_WORKER_LABELS", "rack=r4"}},
         "--label: label key 'rack' is given twice"},
        {{"--master", "a:1"}, {{"SR_WORKER_LABELS", "rack=r4,"}},
         "SR_WORKER_LABELS: label '' has no '='"},
        {{"--master", "a:1", "--label"}, {}, "--label needs a value"},
    };

    for (const RefusedCase& refused : cases)
    {
        const WorkerOptionsResult result = parse(refused.arguments, refused.variables);

        EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid) << refused.expected;
        EXPECT_NE(result.message.find(refused.expected), std::string::npos)
            << "expected '" << refused.expected << "' in '" << result.message << "'";
        EXPECT_EQ(result.message.find('\n'), std::string::npos) << result.message;
    }
}

/** @brief Tests that a key or a value HELLO cannot carry is refused. */
TEST(Medium_WorkerOptionsParserTest, ALabelAbove255BytesIsRefused)
{
    const std::string label = "key=" + std::string(256, 'v');

    const WorkerOptionsResult result = parse({"--master", "a:1", "--label", label.c_str()});

    EXPECT_EQ(result.status, WorkerOptionsStatus::Invalid);
    EXPECT_NE(result.message.find("255 bytes"), std::string::npos) << result.message;
}
