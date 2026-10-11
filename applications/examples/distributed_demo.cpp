/**************************************************************************/
/* distributed_demo.cpp                                                   */
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
 * @file distributed_demo.cpp
 * @brief Runs the master of the distributed demo: one vector add a worker may take over.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/distributed/distributed.hpp>

#include "distributed_kernels.hpp"

using namespace SushiRuntime;

namespace
{
    /** @brief Holds the settings of the master, one field per command line flag. */
    struct DemoOptions
    {
        /** @brief `--bind` and `--port`: where workers connect. */
        Distributed::Endpoint listen{"127.0.0.1", 5555};

        /** @brief `--secret-file`: the cluster's key; empty attaches without one. */
        std::filesystem::path secret_file;

        /** @brief `--wait-s`: how long to wait for a worker before the task runs here. */
        std::chrono::seconds worker_wait{30};

        /** @brief `--elements`: the length of each vector. */
        std::size_t element_count = std::size_t{1} << 20;
    };

    /** @brief Describes one flag: its name, its help line and how its value is stored. */
    struct Flag
    {
        const char* name;    /**< The flag as typed, with its two dashes. */
        const char* meaning; /**< The line `--help` prints for it. */

        /** @brief Stores @p value in @p options; returns false when the value is refused. */
        bool (*apply)(std::string_view value, DemoOptions& options);
    };

    /**
     * @brief Reads a decimal number without a sign.
     * @param maximum The largest value accepted; at most 2^32.
     * @return Nothing when @p text is empty, holds another character or exceeds @p maximum.
     */
    std::optional<std::uint64_t> parse_whole_number(std::string_view text, std::uint64_t maximum)
    {
        if (text.empty())
            return std::nullopt;

        std::uint64_t value = 0;
        for (const char digit : text)
        {
            if (digit < '0' || digit > '9')
                return std::nullopt;

            value = value * 10 + static_cast<std::uint64_t>(digit - '0');
            if (value > maximum)
                return std::nullopt;
        }
        return value;
    }

    /** @brief Lists every flag that takes a value; `--help` is printed from this table. */
    const Flag K_FLAGS[] = {
        {"--bind", "address to listen on (127.0.0.1)",
         [](std::string_view value, DemoOptions& options)
         {
             options.listen.host = std::string(value);
             return !value.empty();
         }},
        {"--port", "port to listen on, 0 for any free port (5555)",
         [](std::string_view value, DemoOptions& options)
         {
             const std::optional<std::uint64_t> port = parse_whole_number(value, 65535);
             if (port.has_value())
                 options.listen.port = static_cast<std::uint16_t>(*port);
             return port.has_value();
         }},
        {"--secret-file", "file of at least 32 bytes that workers must hold too (none)",
         [](std::string_view value, DemoOptions& options)
         {
             options.secret_file = std::filesystem::path(std::string(value));
             return !value.empty();
         }},
        {"--wait-s", "seconds to wait for a worker, 0 to run here at once (30)",
         [](std::string_view value, DemoOptions& options)
         {
             const std::optional<std::uint64_t> seconds = parse_whole_number(value, 86400);
             if (seconds.has_value())
                 options.worker_wait = std::chrono::seconds(*seconds);
             return seconds.has_value();
         }},
        {"--elements", "length of each vector, from 1 to 67108864 (1048576)",
         [](std::string_view value, DemoOptions& options)
         {
             const std::optional<std::uint64_t> count = parse_whole_number(value, 67108864);
             if (count.has_value())
                 options.element_count = static_cast<std::size_t>(*count);
             return count.has_value() && *count > 0;
         }},
    };

    /** @brief Holds the outcome of reading the command line. */
    struct CommandLine
    {
        DemoOptions options;     /**< The settings; meaningful when error is empty. */
        bool wants_help = false; /**< Whether `--help` was given. */
        std::string error;       /**< What was wrong with which argument; empty when none. */
    };

    /** @brief Returns the flag of a name, or null when the table has none. */
    const Flag* find_flag(std::string_view name)
    {
        for (const Flag& flag : K_FLAGS)
        {
            if (name == flag.name)
                return &flag;
        }
        return nullptr;
    }

    /** @brief Reads the flags; a value follows its flag as the next argument or after `=`. */
    CommandLine read_command_line(int argc, char** argv)
    {
        CommandLine line;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (argument == "--help")
            {
                line.wants_help = true;
                return line;
            }

            const std::size_t equals = argument.find('=');
            const std::string name(argument.substr(0, equals));
            const Flag* flag = find_flag(name);
            if (flag == nullptr)
            {
                line.error = "unknown argument '" + std::string(argument) + "'";
                return line;
            }
            if (equals == std::string_view::npos && index + 1 >= argc)
            {
                line.error = name + " needs a value";
                return line;
            }

            const std::string_view value = equals == std::string_view::npos
                                               ? std::string_view(argv[++index])
                                               : argument.substr(equals + 1);
            if (!flag->apply(value, line.options))
            {
                line.error = name + ": '" + std::string(value) + "' is not accepted";
                return line;
            }
        }
        return line;
    }

    /** @brief Prints the flags and their defaults. */
    void print_usage()
    {
        std::printf("usage: sr_distributed_demo [--flag value]...\n");
        for (const Flag& flag : K_FLAGS)
            std::printf("  %-14s %s\n", flag.name, flag.meaning);
    }

    /**
     * @brief Reads the cluster's key, or returns the empty secret when no file was named.
     * @return The secret, or why the file cannot be read or holds fewer than 32 bytes.
     */
    Result<Distributed::SharedSecret> read_secret(const std::filesystem::path& file)
    {
        if (file.empty())
            return Distributed::SharedSecret();
        return Distributed::SharedSecret::from_file(file);
    }

    /**
     * @brief Adds two vectors of @p count floats and checks every element of the sum.
     * @pre A master is attached to @p runtime, so distributed() has a coordinator to share with.
     * @return True when the sum is right, wherever the task ran.
     */
    bool run_vector_add(Runtime& runtime, std::size_t count)
    {
        auto a = runtime.buffer<float>(count).value().distributed();
        auto b = runtime.buffer<float>(count).value().distributed();
        auto sum = runtime.buffer<float>(count).value().distributed();

        const float* a_data = a.data();
        const float* b_data = b.data();
        float* sum_data = sum.data();
        for (std::size_t index = 0; index < count; ++index)
        {
            a[index] = static_cast<float>(index);
            b[index] = static_cast<float>(2 * index);
            sum[index] = 0.0f;
        }

        // The lambda is the local fallback; a worker runs the kernel registered for the same id.
        auto graph = runtime.graph();
        graph.add(DistributedDemo::K_VECTOR_ADD, count, Reads(a, b), Writes(sum),
                  [a_data, b_data, sum_data](std::size_t index)
                  {
                      DistributedDemo::add_element(a_data, b_data, sum_data, index);
                  });
        graph.run().value();

        for (std::size_t index = 0; index < count; ++index)
        {
            if (sum[index] != a[index] + b[index])
                return false;
        }
        return true;
    }

    /** @brief Prints one line per registered worker. */
    void print_workers(const Distributed::ClusterStatistics& statistics)
    {
        for (const Distributed::WorkerSnapshot& worker : statistics.workers)
            std::printf("  worker %u \"%s\" at %s\n", static_cast<unsigned>(worker.id),
                        worker.name.c_str(), worker.endpoint.c_str());
    }

    /** @brief Prints where the task ran and the master's counters. */
    void print_outcome(const Distributed::ClusterStatistics& statistics)
    {
        std::printf("  ran on: %s\n",
                    statistics.completed_remote > 0 ? "a worker" : "this machine");
        std::printf("  offloaded=%llu declined=%llu redispatched=%llu stale_writebacks=%llu\n",
                    static_cast<unsigned long long>(statistics.offloaded),
                    static_cast<unsigned long long>(statistics.declined),
                    static_cast<unsigned long long>(statistics.redispatched),
                    static_cast<unsigned long long>(statistics.stale_writebacks));
        std::printf("  bytes_sent=%llu bytes_received=%llu chunks_sent=%llu chunks_received=%llu\n",
                    static_cast<unsigned long long>(statistics.bytes_sent),
                    static_cast<unsigned long long>(statistics.bytes_received),
                    static_cast<unsigned long long>(statistics.chunks_sent),
                    static_cast<unsigned long long>(statistics.chunks_received));
    }

    /**
     * @brief Attaches a master, waits for a worker, runs the vector add and reports.
     * @return 0 when the sum is right; 1 otherwise, and when the secret cannot be read or
     *         the master does not start.
     */
    int run_demo(const DemoOptions& options)
    {
        auto runtime = Runtime::create().value();

        Distributed::TCPMasterOptions listener;
        listener.bind = options.listen;
        auto transport = Distributed::TCPTransport::make_master(listener);

        auto secret = read_secret(options.secret_file);
        if (!secret)
        {
            std::fprintf(stderr, "distributed_demo: %s
", secret.error().message.c_str());
            return 1;
        }

        Distributed::ClusterOptions cluster_options;
        cluster_options.secret = std::move(*secret);
        const bool is_authenticated = !cluster_options.secret.empty();
        auto attached =
            Distributed::Cluster::attach(runtime, transport, std::move(cluster_options));
        if (!attached)
        {
            std::fprintf(stderr, "distributed_demo: %s
", attached.error().message.c_str());
            return 1;
        }
        Distributed::Cluster& cluster = *attached;

        std::printf("distributed_demo: listening on %s, authentication %s\n",
                    transport->local_endpoint().to_string().c_str(),
                    is_authenticated ? "on" : "off");

        if (cluster.wait_for_workers(1, options.worker_wait))
            print_workers(cluster.statistics());
        else
            std::printf("  no worker within %lld s\n",
                        static_cast<long long>(options.worker_wait.count()));

        const bool is_correct = run_vector_add(runtime, options.element_count);

        std::printf("distributed_demo: N = %zu, result %s\n", options.element_count,
                    is_correct ? "CORRECT" : "WRONG");
        print_outcome(cluster.statistics());
        return is_correct ? 0 : 1;
    }
} // namespace

int main(int argc, char** argv)
{
    const CommandLine line = read_command_line(argc, argv);
    if (line.wants_help)
    {
        print_usage();
        return 0;
    }
    if (!line.error.empty())
    {
        std::fprintf(stderr, "distributed_demo: %s\n", line.error.c_str());
        print_usage();
        return 1;
    }

    try
    {
        return run_demo(line.options);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "distributed_demo: %s\n", error.what());
        return 1;
    }
}
