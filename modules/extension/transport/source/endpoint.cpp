/**************************************************************************/
/* endpoint.cpp                                                           */
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
 * @file endpoint.cpp
 * @brief Parses and formats Endpoint.
 * @author Mustafa Garip
 */

#include <SushiRuntime/transport/endpoint.hpp>

#include <cstddef>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Largest value a TCP port takes. */
            constexpr std::uint32_t K_MAXIMUM_PORT = 65535;

            /** @brief Most digits a port is written with. */
            constexpr std::size_t K_MAXIMUM_PORT_DIGITS = 5;

            /**
             * @brief Builds the result of a failed parse.
             * @param reason What was wrong, without the text.
             * @return A result whose message quotes @p text and names both accepted forms.
             */
            EndpointParseResult rejected(EndpointParseStatus status, std::string_view text,
                                         std::string_view reason)
            {
                EndpointParseResult result;
                result.status = status;
                result.message.append("endpoint '").append(text).append("': ").append(reason);
                result.message.append("; expected host:port or [ipv6]:port");
                return result;
            }

            /**
             * @brief Reads a decimal port.
             * @return Whether @p digits holds one to five digits whose value fits a port.
             */
            bool read_port(std::string_view digits, std::uint16_t& port) noexcept
            {
                if (digits.empty() || digits.size() > K_MAXIMUM_PORT_DIGITS)
                    return false;

                std::uint32_t value = 0;
                for (const char digit : digits)
                {
                    if (digit < '0' || digit > '9')
                        return false;
                    value = value * 10 + static_cast<std::uint32_t>(digit - '0');
                }

                if (value > K_MAXIMUM_PORT)
                    return false;

                port = static_cast<std::uint16_t>(value);
                return true;
            }
        } // namespace

        /**
         * @brief Parses `host:port` or `[ipv6]:port`.
         */
        EndpointParseResult Endpoint::parse(std::string_view text)
        {
            if (text.empty())
                return rejected(EndpointParseStatus::Empty, text, "the text is empty");

            std::string_view host;
            std::string_view port;

            if (text.front() == '[')
            {
                const std::size_t close = text.find(']');
                if (close == std::string_view::npos)
                    return rejected(EndpointParseStatus::UnclosedBracket, text, "'[' has no ']'");

                host = text.substr(1, close - 1);
                const std::string_view tail = text.substr(close + 1);
                if (tail.empty() || tail.front() != ':')
                    return rejected(EndpointParseStatus::MissingPort, text,
                                    "no ':port' follows ']'");
                port = tail.substr(1);
            }
            else
            {
                const std::size_t colon = text.rfind(':');
                if (colon == std::string_view::npos)
                    return rejected(EndpointParseStatus::MissingPort, text, "there is no port");

                host = text.substr(0, colon);
                if (host.find(':') != std::string_view::npos)
                    return rejected(EndpointParseStatus::UnbracketedIpv6, text,
                                    "an IPv6 address needs brackets");
                port = text.substr(colon + 1);
            }

            if (host.empty())
                return rejected(EndpointParseStatus::MissingHost, text, "there is no host");

            EndpointParseResult result;
            if (!read_port(port, result.endpoint.port))
                return rejected(EndpointParseStatus::InvalidPort, text,
                                "the port is not a number from 0 to 65535");

            result.endpoint.host.assign(host);
            return result;
        }

        /**
         * @brief Formats the endpoint for a log line, an IPv6 host in brackets.
         */
        std::string Endpoint::to_string() const
        {
            const bool is_ipv6 = host.find(':') != std::string::npos;

            std::string text;
            text.reserve(host.size() + 8);
            if (is_ipv6)
                text.append("[").append(host).append("]");
            else
                text.append(host);
            text.append(":").append(std::to_string(port));
            return text;
        }

    } // namespace Distributed
} // namespace SushiRuntime
