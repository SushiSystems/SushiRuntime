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
#include <SushiRuntime/log/log_project.hpp>

namespace SushiRuntime
{
    /**
     * @brief Names the project and the module a record comes from; a module logs only under
     *        its own.
     */
    class LogCategory final
    {
        public:
            /**
             * @brief Holds @p project and @p module, the folder name of the module in lower case.
             */
            constexpr LogCategory(LogProject project, std::string_view module) noexcept
                : project_(project), module_(module)
            {

            }

            /** @brief Returns the project whose variable sets the category's level. */
            [[nodiscard]] constexpr LogProject project() const noexcept
            {
                return project_;
            }

            /** @brief Returns the module name records and the level variable use. */
            [[nodiscard]] constexpr std::string_view module() const noexcept
            {
                return module_;
            }

        private:
            /// @brief The project the module belongs to.
            LogProject project_;
            /// @brief The folder name of the module.
            std::string_view module_;
    };

    /**
     * @brief Returns the category of @p module in @p project, for a K_<MODULE>_LOG_CATEGORY.
     */
    [[nodiscard]] constexpr LogCategory make_log_category(LogProject project,
                                                          std::string_view module) noexcept
    {
        return LogCategory(project, module);
    }

    /**
     * @brief Sets the level of one category, which then ignores set_log_level(project, level).
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_level(LogCategory category, LogLevel level) noexcept;

    /**
     * @brief Returns the level of one category.
     * @return What set_log_level() or the project's variable set for it, else the level of its
     *         project.
     */
    [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level(LogCategory category) noexcept;
} // namespace SushiRuntime
