/**************************************************************************/
/* socket_option_report.hpp                                               */
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

/**
 * @file socket_option_report.hpp
 * @brief Declares report_no_delay_failure, the log line of a TCP_NODELAY option that failed.
 * @author Mustafa Garip
 * @see modules/extension/network_socket/README.md
 */

#pragma once

#include <string>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/network_socket/network_socket_log_category.hpp>
#include <SushiRuntime/network_socket/socket.hpp>

namespace SushiRuntime
{
    namespace Distributed::Detail
    {
        /**
         * @brief Logs one WARN for a connection whose TCP_NODELAY option the OS refused.
         *
         * A connection sets the option once, so the line is written once per peer.
         * @param endpoint Names the peer, or the listener that accepted the connection.
         * @param os_error The errno or WSAGetLastError() value of the failed call.
         */
        inline void report_no_delay_failure(const std::string& endpoint, int os_error)
        {
            SR_LOG_WARN(K_NETWORK_SOCKET_LOG_CATEGORY,
                        "TCP_NODELAY could not be set on the connection with {}: {} (os error "
                        "{}); frames wait for Nagle's delay on this connection.",
                        endpoint, describe_os_error(os_error), os_error);
        }
    } // namespace Distributed::Detail
} // namespace SushiRuntime
