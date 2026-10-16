/**************************************************************************/
/* host_hooks_demo.cpp                                                    */
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
 * @file host_hooks_demo.cpp
 * @brief Installs a host's assertion handler and log sink around a run, then restores both.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <SushiLog/log/log_calls.hpp>
#include <SushiLog/log/log_level.hpp>
#include <SushiLog/log/log_record.hpp>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Elements of the buffer the run fills. */
    constexpr std::size_t K_COUNT = 1024;

    /** @brief Writes a broken rule in the host's format; the process stops after it returns. */
    void report_broken_rule(const AssertionFailure& failure)
    {
        std::fprintf(stderr, "host: SushiRuntime stopped the process: %s (%s:%d)\n",
                     failure.message != nullptr ? failure.message : "no message",
                     failure.file != nullptr ? failure.file : "unknown file", failure.line);
    }

    /** @brief Runs a graph with the host's hooks installed; returns 0 when both were in effect. */
    int run_host_hooks_demo()
    {
        // Runtime::create() stops the process on a library of another release than the headers.
        if (!version_matches())
        {
            std::fprintf(stderr, "host_hooks_demo: headers are %s, the loaded library is %s\n",
                         SUSHIRUNTIME_VERSION_STRING, runtime_version_string());
            return 1;
        }

        // A broken rule calls this handler before the process stops; the call returns the old one.
        const AssertionHandler default_handler = set_assertion_handler(&report_broken_rule);

        // The sink receives every record at or below the level, on the thread that logged it.
        std::atomic<std::size_t> lines{0};
        std::atomic<std::size_t>* const counter = &lines;
        const SushiLog::LogLevel default_level = SushiLog::log_level(K_SUSHIRUNTIME_LOG_PROJECT);
        SushiLog::set_log_synchronous(true);
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, SushiLog::LogLevel::Info);
        SushiLog::set_log_sink([counter](const SushiLog::LogRecord&)
        {
            counter->fetch_add(1, std::memory_order_relaxed);
        });

        std::uint32_t last = 0;
        {
            auto runtime = Runtime::create().value();
            auto values = runtime.buffer<std::uint32_t>(K_COUNT).value();
            auto graph = runtime.graph();
            graph.add(K_COUNT, Out(values),
                      [](std::size_t i, std::uint32_t* out)
                      {
                          out[i] = static_cast<std::uint32_t>(i) * 2u;
                      })
                .name("double_index");
            graph.run().value();
            last = values[K_COUNT - 1];
        }

        // An empty sink restores the default one, which writes to standard error.
        SushiLog::set_log_sink(nullptr);
        SushiLog::set_log_level(K_SUSHIRUNTIME_LOG_PROJECT, default_level);
        SushiLog::set_log_synchronous(false);
        const AssertionHandler replaced = set_assertion_handler(default_handler);

        const bool saw_lines = lines.load() > 0;
        const bool was_installed = replaced == &report_broken_rule;
        const bool is_correct = saw_lines && was_installed && last == 2 * (K_COUNT - 1);

        std::printf("host hooks: headers and library are of one release\n");
        std::printf("  the sink received the runtime's log lines: %s\n", saw_lines ? "yes" : "no");
        std::printf("  the assertion handler was the host's until restored: %s\n",
                    was_installed ? "yes" : "no");
        std::printf("  values[%zu] = %u  (expected %zu)\n", K_COUNT - 1, last, 2 * (K_COUNT - 1));
        std::printf("  result: %s\n", is_correct ? "CORRECT" : "WRONG");
        return is_correct ? 0 : 1;
    }
} // namespace

int main()
{
    return run_host_hooks_demo();
}
