/**************************************************************************/
/* log_project.hpp                                                        */
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
 * @file log_project.hpp
 * @brief Declares LogProject and the calls that set and read the level of one project.
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
     * @brief Names a project that logs and the environment variable that sets its levels.
     *
     * Both views refer to string literals, so a project lives for the whole process.
     */
    class LogProject final
    {
        public:
            /**
             * @brief Holds @p name, the project in lower case, and @p level_variable, the
             *        variable its levels are read from, such as SB_LOG_LEVEL.
             */
            constexpr LogProject(std::string_view name, std::string_view level_variable) noexcept
                : name_(name), level_variable_(level_variable)
            {

            }

            /** @brief Returns the name a record shows before the module. */
            [[nodiscard]] constexpr std::string_view name() const noexcept
            {
                return name_;
            }

            /** @brief Returns the environment variable that sets the project's levels. */
            [[nodiscard]] constexpr std::string_view level_variable() const noexcept
            {
                return level_variable_;
            }

        private:
            /// @brief The project name, in lower case.
            std::string_view name_;
            /// @brief The environment variable read on the first use of the project.
            std::string_view level_variable_;
    };

    /**
     * @brief Sets every category of @p project except those set one by one.
     * @see modules/foundation/log/README.md
     */
    SUSHIRUNTIME_API void set_log_level(LogProject project, LogLevel level) noexcept;

    /**
     * @brief Returns the level of the categories of @p project that no module setting overrides.
     * @return LogLevel::Warn, or the level the project's variable names, until set_log_level()
     *         is called for the project.
     */
    [[nodiscard]] SUSHIRUNTIME_API LogLevel log_level(LogProject project) noexcept;
} // namespace SushiRuntime
