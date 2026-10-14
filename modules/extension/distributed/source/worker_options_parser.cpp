/**************************************************************************/
/* worker_options_parser.cpp                                              */
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
 * @file worker_options_parser.cpp
 * @brief Implements parse_worker_options() from one table of flags.
 * @author Mustafa Garip
 */

#include <SushiRuntime/distributed/worker_options_parser.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace SushiRuntime
{
    namespace Distributed
    {
        namespace
        {
            /** @brief Holds the longest worker name HELLO can carry, in bytes. */
            constexpr std::size_t K_MAXIMUM_NAME_BYTES = 255;

            /** @brief Holds the largest count of milliseconds or seconds a flag accepts. */
            constexpr std::uint64_t K_MAXIMUM_DURATION_COUNT =
                static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());

            /**
             * @brief Reads a decimal count inside a range.
             * @param problem Receives what was wrong with the text when the call fails.
             */
            [[nodiscard]] bool read_count(std::string_view text, std::uint64_t minimum,
                                          std::uint64_t maximum, std::uint64_t& value,
                                          std::string& problem)
            {
                const char* const end = text.data() + text.size();
                std::uint64_t parsed = 0;
                const std::from_chars_result read = std::from_chars(text.data(), end, parsed);
                if (read.ec == std::errc() && read.ptr == end && !text.empty() &&
                    parsed >= minimum && parsed <= maximum)
                {
                    value = parsed;
                    return true;
                }

                problem = "'" + std::string(text) + "' is not a whole number from " +
                          std::to_string(minimum) + " to " + std::to_string(maximum);
                return false;
            }

            /** @brief Stores the master's endpoint; port 0 names no master. */
            [[nodiscard]] bool apply_master(std::string_view text, WorkerOptions& options,
                                            std::string& problem)
            {
                EndpointParseResult parsed = Endpoint::parse(text);
                if (!parsed.is_ok())
                {
                    problem = std::move(parsed.message);
                    return false;
                }

                if (parsed.endpoint.port == 0)
                {
                    problem = "'" + std::string(text) + "' has port 0, which no master listens on";
                    return false;
                }

                options.master = std::move(parsed.endpoint);
                return true;
            }

            /** @brief Stores the path of the secret file. */
            [[nodiscard]] bool apply_secret_file(std::string_view text, WorkerOptions& options,
                                                 std::string&)
            {
                options.secret_file = std::filesystem::path(std::string(text));
                return true;
            }

            /** @brief Stores the worker's name when HELLO can carry it. */
            [[nodiscard]] bool apply_name(std::string_view text, WorkerOptions& options,
                                          std::string& problem)
            {
                if (text.size() > K_MAXIMUM_NAME_BYTES)
                {
                    problem = "the name holds " + std::to_string(text.size()) +
                              " bytes; a worker name holds at most " +
                              std::to_string(K_MAXIMUM_NAME_BYTES);
                    return false;
                }

                options.name = std::string(text);
                return true;
            }

            /** @brief Stores the job queue's bound, which PROOF carries in 32 bits. */
            [[nodiscard]] bool apply_max_jobs(std::string_view text, WorkerOptions& options,
                                              std::string& problem)
            {
                std::uint64_t value = 0;
                if (!read_count(text, 1, std::numeric_limits<std::uint32_t>::max(), value,
                                problem))
                    return false;

                options.max_jobs = static_cast<std::size_t>(value);
                return true;
            }

            /** @brief Stores the cache's byte budget. */
            [[nodiscard]] bool apply_cache_bytes(std::string_view text, WorkerOptions& options,
                                                 std::string& problem)
            {
                return read_count(text, 0, std::numeric_limits<std::uint64_t>::max(),
                                  options.cache_bytes, problem);
            }

            /** @brief Stores the heartbeat interval, at least one millisecond. */
            [[nodiscard]] bool apply_heartbeat(std::string_view text, WorkerOptions& options,
                                               std::string& problem)
            {
                std::uint64_t value = 0;
                if (!read_count(text, 1, K_MAXIMUM_DURATION_COUNT, value, problem))
                    return false;

                options.heartbeat_interval = std::chrono::milliseconds(value);
                return true;
            }

            /** @brief Stores the pull idle timeout; 0 waits without a limit. */
            [[nodiscard]] bool apply_pull_idle_timeout(std::string_view text,
                                                       WorkerOptions& options,
                                                       std::string& problem)
            {
                std::uint64_t value = 0;
                if (!read_count(text, 0, K_MAXIMUM_DURATION_COUNT, value, problem))
                    return false;

                options.pull_idle_timeout = std::chrono::milliseconds(value);
                return true;
            }

            /** @brief Stores the connect timeout; 0 waits without a limit. */
            [[nodiscard]] bool apply_connect_timeout(std::string_view text,
                                                     WorkerOptions& options,
                                                     std::string& problem)
            {
                std::uint64_t value = 0;
                if (!read_count(text, 0, K_MAXIMUM_DURATION_COUNT, value, problem))
                    return false;

                options.connect_timeout = std::chrono::seconds(value);
                return true;
            }

            /** @brief Stores the log level's name as written. */
            [[nodiscard]] bool apply_log_level(std::string_view text, WorkerOptions& options,
                                               std::string&)
            {
                options.log_level = std::string(text);
                return true;
            }

            /** @brief Clears the reconnect setting. */
            void apply_no_reconnect(WorkerOptions& options)
            {
                options.reconnect = false;
            }

            /** @brief Describes one flag that takes a value. */
            struct ValueOption
            {
                /** @brief The flag as typed, with its dashes. */
                std::string_view flag;

                /** @brief The word the usage text shows for the value. */
                std::string_view value_name;

                /** @brief The environment variable behind the flag, or null for none. */
                const char* variable;

                /** @brief What the usage text says about the flag. */
                std::string_view help;

                /** @brief Stores the value, or names what is wrong with it. */
                bool (*apply)(std::string_view text, WorkerOptions& options, std::string& problem);
            };

            /** @brief Describes one flag that takes no value. */
            struct SwitchOption
            {
                /** @brief The flag as typed, with its dashes. */
                std::string_view flag;

                /** @brief What the usage text says about the flag. */
                std::string_view help;

                /** @brief What the flag asks for; Run keeps reading the command line. */
                WorkerOptionsStatus status;

                /** @brief Changes the settings, or null when the status says it all. */
                void (*apply)(WorkerOptions& options);
            };

            /** @brief Lists the flags that take a value, in the order the usage text shows. */
            constexpr std::array<ValueOption, 9> K_VALUE_OPTIONS = {{
                {"--master", "host:port", "SR_MASTER", "the master to join; required",
                 apply_master},
                {"--secret-file", "path", "SR_CLUSTER_SECRET_FILE",
                 "file that holds the cluster secret; default: none", apply_secret_file},
                {"--name", "text", "SR_WORKER_NAME",
                 "name in the master's log; default: the host name", apply_name},
                {"--max-jobs", "n", "SR_WORKER_MAX_JOBS",
                 "tasks that may wait to run; default 1024", apply_max_jobs},
                {"--cache-bytes", "n", "SR_WORKER_CACHE_BYTES",
                 "bytes of cached buffers; default 0, half of device memory",
                 apply_cache_bytes},
                {"--heartbeat-ms", "n", "SR_WORKER_HEARTBEAT_MS",
                 "time between two heartbeats; default 1000", apply_heartbeat},
                {"--pull-idle-timeout-ms", "n", "SR_WORKER_PULL_IDLE_TIMEOUT_MS",
                 "silence that fails a buffer pull; default 5000, 0 waits without a limit",
                 apply_pull_idle_timeout},
                {"--connect-timeout-s", "n", "SR_WORKER_CONNECT_TIMEOUT_S",
                 "time allowed to reach the master; default 60, 0 waits without a limit",
                 apply_connect_timeout},
                {"--log-level", "name", nullptr, "off, error, warn, info or debug",
                 apply_log_level},
            }};

            /** @brief Lists the flags that take no value, in the order the usage text shows. */
            constexpr std::array<SwitchOption, 3> K_SWITCH_OPTIONS = {{
                {"--no-reconnect", "exit with code 5 when the master is lost",
                 WorkerOptionsStatus::Run, apply_no_reconnect},
                {"--version", "print the protocol version and the build, then exit",
                 WorkerOptionsStatus::Version, nullptr},
                {"--help", "print this text, then exit", WorkerOptionsStatus::Help, nullptr},
            }};

            /** @brief Returns the slot of a value flag, or the table's size when none matches. */
            [[nodiscard]] std::size_t find_value_option(std::string_view flag) noexcept
            {
                std::size_t slot = 0;
                while (slot < K_VALUE_OPTIONS.size() && K_VALUE_OPTIONS[slot].flag != flag)
                    ++slot;
                return slot;
            }

            /** @brief Returns the switch a flag names, or null. */
            [[nodiscard]] const SwitchOption* find_switch_option(std::string_view flag) noexcept
            {
                for (const SwitchOption& option : K_SWITCH_OPTIONS)
                {
                    if (option.flag == flag)
                        return &option;
                }
                return nullptr;
            }

            /** @brief Returns the usage text, one line per flag of the two tables. */
            [[nodiscard]] std::string make_usage()
            {
                std::string usage = "usage: <worker> --master host:port [flags]\n"
                                    "A flag beats its environment variable, which beats the "
                                    "default.\n";
                for (const ValueOption& option : K_VALUE_OPTIONS)
                {
                    usage.append("  ").append(option.flag).append(" ").append(option.value_name);
                    if (option.variable != nullptr)
                        usage.append(" (").append(option.variable).append(")");
                    usage.append("\n      ").append(option.help).append("\n");
                }

                for (const SwitchOption& option : K_SWITCH_OPTIONS)
                {
                    usage.append("  ").append(option.flag);
                    usage.append("\n      ").append(option.help).append("\n");
                }
                return usage;
            }

            /** @brief Returns a result that refuses the command line with one line. */
            [[nodiscard]] WorkerOptionsResult refuse(std::string message)
            {
                WorkerOptionsResult result;
                result.status = WorkerOptionsStatus::Invalid;
                result.message = std::move(message);
                return result;
            }
        } // namespace

        WorkerOptionsResult parse_worker_options(SushiRuntime::span<const char* const> arguments,
                                                 const EnvironmentReader& environment)
        {
            WorkerOptionsResult result;
            std::array<std::optional<std::string_view>, K_VALUE_OPTIONS.size()> given;

            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                const std::string_view argument(arguments[index]);
                const std::size_t equals = argument.find('=');
                const std::string_view flag = argument.substr(0, equals);

                if (const SwitchOption* const option = find_switch_option(flag))
                {
                    if (equals != std::string_view::npos)
                        return refuse(std::string(flag) + " takes no value");

                    if (option->status != WorkerOptionsStatus::Run)
                    {
                        result.status = option->status;
                        if (option->status == WorkerOptionsStatus::Help)
                            result.message = make_usage();
                        return result;
                    }

                    option->apply(result.options);
                    continue;
                }

                const std::size_t slot = find_value_option(flag);
                if (slot == K_VALUE_OPTIONS.size())
                    return refuse("unknown argument '" + std::string(argument) +
                                  "'; --help lists the flags");

                if (equals != std::string_view::npos)
                    given[slot] = argument.substr(equals + 1);
                else if (index + 1 < arguments.size())
                    given[slot] = std::string_view(arguments[++index]);
                else
                    return refuse(std::string(flag) + " needs a value");
            }

            for (std::size_t slot = 0; slot < K_VALUE_OPTIONS.size(); ++slot)
            {
                const ValueOption& option = K_VALUE_OPTIONS[slot];
                std::string source(option.flag);
                std::optional<std::string> text;
                if (given[slot])
                {
                    text = std::string(*given[slot]);
                }
                else if (option.variable != nullptr)
                {
                    text = environment(option.variable);
                    source = option.variable;
                }

                if (!text || (!given[slot] && text->empty()))
                    continue;

                std::string problem;
                if (!option.apply(*text, result.options, problem))
                    return refuse(source + ": " + problem);
            }

            if (result.options.master.host.empty())
                return refuse("no master given; pass --master host:port or set SR_MASTER");

            return result;
        }

    } // namespace Distributed
} // namespace SushiRuntime
