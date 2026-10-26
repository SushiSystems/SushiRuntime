/**************************************************************************/
/* stop_signal.cpp                                                        */
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
 * @file stop_signal.cpp
 * @brief Implements StopSignal over the C runtime's signal(), the same call on every platform.
 * @author Mustafa Garip
 */

#include <SushiRuntime/process_environment/stop_signal.hpp>

#include <atomic>
#include <csignal>
#include <cerrno>
#include <cstdlib>

#include <SushiRuntime/log/logger.hpp>
#include <SushiRuntime/process_environment/process_environment_log_category.hpp>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the exit code of a process ended by its second stop signal. */
            constexpr int K_SECOND_SIGNAL_EXIT_CODE = 130;

            /** @brief Lists the signals that ask a worker to stop on this platform. */
            constexpr int K_STOP_SIGNALS[] = {
                SIGINT,
                SIGTERM,
#if defined(SIGBREAK)
                SIGBREAK,
#endif
            };

            /** @brief Counts the entries of K_STOP_SIGNALS. */
            constexpr std::size_t K_STOP_SIGNAL_COUNT =
                sizeof(K_STOP_SIGNALS) / sizeof(K_STOP_SIGNALS[0]);

            /** @brief Holds whether a stop was asked for; the one object the handler touches. */
            std::atomic<bool> stop_requested{false};

            static_assert(std::atomic<bool>::is_always_lock_free,
                          "the stop flag is written from a signal handler");

            /** @brief Installs through the C runtime's signal(); the default installer. */
            StopSignal::Handler install_with_c_runtime(int signal_number,
                                                       StopSignal::Handler handler)
            {
                return std::signal(signal_number, handler);
            }
        } // namespace

        extern "C"
        {
            /** @brief Sets the flag on the first signal and ends the process on the second. */
            static void handle_stop_signal(int signal_number)
            {
                if (stop_requested.exchange(true))
                    std::_Exit(K_SECOND_SIGNAL_EXIT_CODE);

                // A C runtime may reset the disposition before the call; this arms it again.
                (void)std::signal(signal_number, handle_stop_signal);
            }
        }

        StopSignal::StopSignal() noexcept : StopSignal(&install_with_c_runtime)
        {
        }

        StopSignal::StopSignal(SignalInstaller install_signal) noexcept
        {
            static_assert(K_STOP_SIGNAL_COUNT <= K_MAXIMUM_SIGNALS,
                          "previous_ has a slot for every stop signal");

            stop_requested.store(false);
            for (std::size_t index = 0; index < K_STOP_SIGNAL_COUNT; ++index)
            {
                previous_[index] = install_signal(K_STOP_SIGNALS[index], handle_stop_signal);
                if (previous_[index] == SIG_ERR)
                {
                    SR_LOG_WARN(K_PROCESS_ENVIRONMENT_LOG_CATEGORY,
                                "signal {} could not be installed (errno {}); no handler is set "
                                "for it and the worker will not stop on it.",
                                K_STOP_SIGNALS[index], errno);
                }
            }
        }

        StopSignal::~StopSignal()
        {
            for (std::size_t index = 0; index < K_STOP_SIGNAL_COUNT; ++index)
            {
                if (previous_[index] != SIG_ERR)
                    (void)std::signal(K_STOP_SIGNALS[index], previous_[index]);
            }
        }

        bool StopSignal::is_requested() const noexcept
        {
            return stop_requested.load();
        }

        void StopSignal::request() noexcept
        {
            stop_requested.store(true);
        }

    } // namespace Distributed
} // namespace SushiRuntime
