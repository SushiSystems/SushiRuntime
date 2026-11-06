/**************************************************************************/
/* test_endpoint.cpp                                                      */
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
 * @file test_endpoint.cpp
 * @brief Tests Endpoint: parsing, the failure message and formatting back.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <cctype>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/endpoint.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/**
 * @brief Checks each accepted and each refused text form against its status, host and port.
 */
TEST(Small_EndpointTest, ParseTable)
{
    /** @brief One line of the parse table. */
    struct Row
    {
        const char*         text;   /**< Text given to the parser. */
        EndpointParseStatus status; /**< Status the parser must return. */
        const char*         host;   /**< Host expected when the status is Ok. */
        std::uint16_t       port;   /**< Port expected when the status is Ok. */
    };

    const Row rows[] = {
        { "localhost:5555",    EndpointParseStatus::Ok,               "localhost",    5555 },
        { "10.0.0.7:0",        EndpointParseStatus::Ok,               "10.0.0.7",     0 },
        { "node-3.lan:65535",  EndpointParseStatus::Ok,               "node-3.lan",   65535 },
        { "[::1]:5555",        EndpointParseStatus::Ok,               "::1",          5555 },
        { "[fe80::1%eth0]:9",  EndpointParseStatus::Ok,               "fe80::1%eth0", 9 },
        { "",                  EndpointParseStatus::Empty,            "",             0 },
        { "localhost",         EndpointParseStatus::MissingPort,      "",             0 },
        { "[::1]",             EndpointParseStatus::MissingPort,      "",             0 },
        { "[::1]5555",         EndpointParseStatus::MissingPort,      "",             0 },
        { ":5555",             EndpointParseStatus::MissingHost,      "",             0 },
        { "[]:5555",           EndpointParseStatus::MissingHost,      "",             0 },
        { "[::1:5555",         EndpointParseStatus::UnclosedBracket,  "",             0 },
        { "::1:5555",          EndpointParseStatus::UnbracketedIpv6,  "",             0 },
        { "localhost:",        EndpointParseStatus::InvalidPort,      "",             0 },
        { "localhost:65536",   EndpointParseStatus::InvalidPort,      "",             0 },
        { "localhost:123456",  EndpointParseStatus::InvalidPort,      "",             0 },
        { "localhost:55a5",    EndpointParseStatus::InvalidPort,      "",             0 },
        { "localhost:-1",      EndpointParseStatus::InvalidPort,      "",             0 },
        { "localhost: 80",     EndpointParseStatus::InvalidPort,      "",             0 },
    };

    for (const Row& row : rows)
    {
        const EndpointParseResult result = Endpoint::parse(row.text);
        EXPECT_EQ(result.status, row.status) << "text '" << row.text << "'";
        EXPECT_EQ(result.is_ok(), result.message.empty()) << "text '" << row.text << "'";
        if (result.is_ok())
        {
            EXPECT_EQ(result.endpoint.host, row.host);
            EXPECT_EQ(result.endpoint.port, row.port);
        }
    }
}

/**
 * @brief Checks that a refusal quotes the text it refused and names both accepted forms.
 */
TEST(Small_EndpointTest, ParseFailureSaysWhatWasWrongWithWhichText)
{
    const EndpointParseResult result = Endpoint::parse("master.lan:http");

    ASSERT_EQ(result.status, EndpointParseStatus::InvalidPort);
    EXPECT_NE(result.message.find("'master.lan:http'"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("port"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("host:port"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("[ipv6]:port"), std::string::npos) << result.message;
}

namespace
{
    /** @brief Holds an endpoint and the text its log form must be. */
    struct FormatRow
    {
        Endpoint endpoint; /**< The endpoint to format. */
        const char* text;  /**< Its log form. */
    };

    /** @brief Names a row by its text in a test report. */
    std::string name_of_format(const ::testing::TestParamInfo<FormatRow>& info)
    {
        std::string name;
        for (const char letter : std::string(info.param.text))
            name += std::isalnum(static_cast<unsigned char>(letter)) != 0 ? letter : '_';
        return name;
    }

    const FormatRow K_FORMATS[] = {
        {{"worker-1", 5555}, "worker-1:5555"},
        {{"192.168.1.20", 1}, "192.168.1.20:1"},
        {{"::1", 65535}, "[::1]:65535"},
    };
} // namespace

/** @brief Runs a test once per endpoint. */
class Small_EndpointFormat : public ::testing::TestWithParam<FormatRow>
{
};

/** @brief Checks that the log form is host:port, with an IPv6 host in brackets. */
TEST_P(Small_EndpointFormat, FormatIsTheHostPortForm)
{
    EXPECT_EQ(GetParam().endpoint.to_string(), GetParam().text);
}

/** @brief Checks that the log form of an endpoint parses back to the same endpoint. */
TEST_P(Small_EndpointFormat, FormatParsesBack)
{
    const Endpoint& endpoint = GetParam().endpoint;

    const EndpointParseResult parsed = Endpoint::parse(endpoint.to_string());

    ASSERT_TRUE(parsed.is_ok()) << parsed.message;
    EXPECT_EQ(parsed.endpoint.host, endpoint.host);
    EXPECT_EQ(parsed.endpoint.port, endpoint.port);
}

INSTANTIATE_TEST_SUITE_P(Rows, Small_EndpointFormat, ::testing::ValuesIn(K_FORMATS),
                         name_of_format);
