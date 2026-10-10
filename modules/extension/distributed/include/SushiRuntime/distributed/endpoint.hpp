/**************************************************************************/
/* endpoint.hpp                                                           */
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
 * @file endpoint.hpp
 * @brief Declares Endpoint, a host and a port, and the parser of its text form.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        struct EndpointParseResult;

        /**
         * @brief Names what was wrong with an endpoint text.
         */
        enum class EndpointParseStatus : std::uint8_t
        {
            Ok,              /**< The text parsed. */
            Empty,           /**< The text holds no character. */
            MissingHost,     /**< Nothing stands before the port. */
            MissingPort,     /**< No `:port` follows the host. */
            UnclosedBracket, /**< A `[` has no matching `]`. */
            UnbracketedIpv6, /**< The host holds a `:` and is not written `[host]`. */
            InvalidPort,     /**< The port is not a decimal number from 0 to 65535. */
        };

        /**
         * @brief Holds the host and the port of one end of a TCP connection.
         */
        struct Endpoint
        {
            /** @brief Host name or numeric address, an IPv6 address without its brackets. */
            std::string host;

            /** @brief TCP port; 0 asks a listener for any free port. */
            std::uint16_t port = 0;

            /**
             * @brief Parses `host:port` or `[ipv6]:port`.
             * @return The endpoint, or the status and a message that quotes @p text.
             */
            [[nodiscard]] SUSHIRUNTIME_API static EndpointParseResult parse(std::string_view text);

            /**
             * @brief Formats the endpoint for a log line, an IPv6 host in brackets.
             * @return `host:port` or `[host]:port`; parse() reads it back.
             */
            [[nodiscard]] SUSHIRUNTIME_API std::string to_string() const;
        };

        /**
         * @brief Carries the outcome of Endpoint::parse().
         */
        struct EndpointParseResult
        {
            /** @brief What the parser decided. */
            EndpointParseStatus status = EndpointParseStatus::Ok;

            /** @brief The parsed endpoint; meaningful when status is Ok. */
            Endpoint endpoint;

            /** @brief What was wrong with which text; empty when status is Ok. */
            std::string message;

            /**
             * @brief Returns whether the text parsed.
             */
            [[nodiscard]] bool is_ok() const noexcept
            {
                return status == EndpointParseStatus::Ok;
            }
        };

    } // namespace Distributed
} // namespace SushiRuntime
