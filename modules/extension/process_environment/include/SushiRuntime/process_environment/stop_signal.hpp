/**************************************************************************/
/* stop_signal.hpp                                                        */
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
 * @file stop_signal.hpp
 * @brief Declares StopSignal, which turns SIGINT, SIGTERM and SIGBREAK into one flag.
 * @author Mustafa Garip
 * @see modules/extension/process_environment/README.md
 */

#pragma once

#include <array>
#include <cstddef>

#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        /**
         * @brief Installs the stop handlers of a worker process while it lives.
         *
         * The first signal sets the flag and nothing else. A second signal ends the process
         * at once with exit code 130, from inside the handler, without running destructors.
         * @pre At most one StopSignal exists in the process at a time.
         */
        class SUSHIRUNTIME_API StopSignal final
        {
            public:
                /** @brief Names a handler as the C runtime stores it. */
                using Handler = void (*)(int);

                /** @brief Names a function that installs a handler and returns the old one. */
                using SignalInstaller = Handler (*)(int, Handler);

                /**
                 * @brief Clears the flag and installs the handler for SIGINT, SIGTERM and,
                 *        where the platform defines it, SIGBREAK, through the C runtime's
                 *        signal().
                 *
                 * A signal the C runtime refuses is logged at WARN and left without a handler.
                 */
                StopSignal() noexcept;

                /**
                 * @brief Does what the default constructor does, installing through
                 *        @p install_signal.
                 * @param install_signal Returns SIG_ERR for a signal it could not install.
                 */
                explicit StopSignal(SignalInstaller install_signal) noexcept;

                /** @brief Puts back the handlers that were installed before. */
                ~StopSignal();

                StopSignal(const StopSignal&) = delete;
                StopSignal& operator=(const StopSignal&) = delete;
                StopSignal(StopSignal&&) = delete;
                StopSignal& operator=(StopSignal&&) = delete;

                /** @brief Returns whether a signal or request() asked the process to stop. */
                [[nodiscard]] bool is_requested() const noexcept;

                /**
                 * @brief Sets the flag as the first signal does; any thread may call.
                 *
                 * A signal that arrives afterwards counts as the second.
                 */
                void request() noexcept;

            private:
                /** @brief Counts the signals a platform can have a stop handler for. */
                static constexpr std::size_t K_MAXIMUM_SIGNALS = 3;

                /// @brief The handlers that were installed before, in the order of the signals.
                std::array<Handler, K_MAXIMUM_SIGNALS> previous_{};
        };

    } // namespace Distributed
} // namespace SushiRuntime
