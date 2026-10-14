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
 * @file test_endpoint.cpp
 * @brief Tests Endpoint: parsing, the failure message and formatting back.
 * @see tests/extension/transport/README.md
 * @author Mustafa Garip
 */

#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/transport/endpoint.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/**
 * @brief Checks each accepted and each refused text form against its status, host and port.
 */
TEST(Unit_EndpointTest, ParseTable)
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
TEST(Unit_EndpointTest, ParseFailureSaysWhatWasWrongWithWhichText)
{
    const EndpointParseResult result = Endpoint::parse("master.lan:http");

    ASSERT_EQ(result.status, EndpointParseStatus::InvalidPort);
    EXPECT_NE(result.message.find("'master.lan:http'"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("port"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("host:port"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("[ipv6]:port"), std::string::npos) << result.message;
}

/**
 * @brief Checks that the log form of an endpoint parses back to the same endpoint.
 */
TEST(Unit_EndpointTest, FormatParsesBack)
{
    const Endpoint endpoints[] = { { "worker-1", 5555 }, { "192.168.1.20", 1 }, { "::1", 65535 } };

    EXPECT_EQ(endpoints[0].to_string(), "worker-1:5555");
    EXPECT_EQ(endpoints[2].to_string(), "[::1]:65535");

    for (const Endpoint& endpoint : endpoints)
    {
        const EndpointParseResult parsed = Endpoint::parse(endpoint.to_string());
        ASSERT_TRUE(parsed.is_ok()) << parsed.message;
        EXPECT_EQ(parsed.endpoint.host, endpoint.host);
        EXPECT_EQ(parsed.endpoint.port, endpoint.port);
    }
}
