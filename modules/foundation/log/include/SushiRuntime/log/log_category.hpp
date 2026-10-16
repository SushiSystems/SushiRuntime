/**************************************************************************/
/* log_category.hpp                                                       */
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
 * @file log_category.hpp
 * @brief Declares LogCategory and the calls that set and read the level of one category.
 * @author Mustafa Garip
 * @see modules/foundation/log/README.md
 */

#pragma once

#include <string_view>

#include <SushiRuntime/core/export.hpp>

#include <SushiRuntime/log/log_level.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the module a record comes from; a module logs only under its own.
     */
    class LogCategory final
    {
        public:
            /**
             * @brief Holds @p name, the folder name of the module, in lower case.
             */
            constexpr explicit LogCategory(std::string_view name) noexcept : name_(name)
            {

            }

            /** @brief Returns the name records and SR_LOG_LEVEL use for the category. */
            [[nodiscard]] constexpr std::string_view name() const noexcept
            {
                return name_;
            }

        private:
            /// @brief The folder name of the module.
            std::string_view name_;
    };

    /**
     * @brief Returns the category named @p name, for a module's K_<MODULE>_LOG_CATEGORY.
     */
    [[nodiscard]] constexpr LogCategory make_log_category(std::string_view name) noexcept
    {
        return LogCategory(name);
    }

    /**
     * @brief Sets the level of one category, which then ignores set_log_level(level).
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_level(LogCategory category, LogLevel level) noexcept;

    /**
     * @brief Returns the level of one category.
     * @return What set_log_level() or SR_LOG_LEVEL set for it, else log_level().
     */
    [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level(LogCategory category) noexcept;
} // namespace SushiRuntime
