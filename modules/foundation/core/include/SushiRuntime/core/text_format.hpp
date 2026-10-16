/**************************************************************************/
/* text_format.hpp                                                        */
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
 * @file text_format.hpp
 * @brief Defines sr_format, which fills the {} of a message with its arguments.
 * @author Mustafa Garip
 * @see modules/foundation/core/README.md
 */

#pragma once

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

namespace SushiRuntime
{
    /**
     * @brief Holds the formatter of the messages the runtime builds.
     */
    namespace Detail
    {
        /**
         * @brief Returns @p format with each doubled brace written once.
         */
        inline std::string sr_format(std::string_view format)
        {
            std::string out;
            out.reserve(format.size());
            for (std::size_t i = 0; i < format.size(); ++i)
            {
                const char c = format[i];
                const bool doubled = (c == '{' || c == '}') && i + 1 < format.size() &&
                                     format[i + 1] == c;
                if (doubled)
                    ++i;
                out += c;
            }
            return out;
        }

        /**
         * @brief Returns @p format with each {} replaced by the next argument, in order.
         * @tparam T A type an output stream can write.
         */
        template <typename T, typename... Args>
        std::string sr_format(std::string_view format, const T& value, const Args&... args)
        {
            std::string out;
            for (std::size_t i = 0; i < format.size(); ++i)
            {
                const char c = format[i];
                const bool has_next = i + 1 < format.size();
                if ((c == '{' || c == '}') && has_next && format[i + 1] == c)
                {
                    out += c;
                    ++i;
                    continue;
                }
                if (c == '{' && has_next && format[i + 1] == '}')
                {
                    std::ostringstream os;
                    os << value;
                    out += os.str();
                    out += sr_format(format.substr(i + 2), args...);
                    return out;
                }
                out += c;
            }
            return out;
        }
    } // namespace Detail
} // namespace SushiRuntime
