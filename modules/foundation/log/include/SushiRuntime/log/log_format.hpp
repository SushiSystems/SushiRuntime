/**************************************************************************/
/* log_format.hpp                                                         */
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
 * @file log_format.hpp
 * @brief Declares sr_format, which formats a message as every SR_LOG call does.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <string>
#include <string_view>

#include <SushiLog/log/log_format.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Returns @p format with each {} replaced by the next argument, as a log call
         *        formats its message.
         */
        template <typename... Args>
        [[nodiscard]] std::string sr_format(std::string_view format, const Args&... args)
        {
            return SushiLog::Detail::format_log_message(format, args...);
        }
    } // namespace Detail
} // namespace SushiRuntime
