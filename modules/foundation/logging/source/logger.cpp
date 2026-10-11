/**************************************************************************/
/* logger.cpp                                                             */
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
 * @file logger.cpp
 * @brief Implements the process-wide log level, the sink and the thread that feeds it.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

// The Microsoft C runtime marks std::getenv deprecated unless this is defined first.
#define _CRT_SECURE_NO_WARNINGS

#include <SushiRuntime/logging/logger.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>

namespace SushiRuntime
{
    namespace Core
    {
        namespace
        {
            /** @brief Holds the level of a process that sets none. */
            constexpr LogLevel K_DEFAULT_LOG_LEVEL = LogLevel::Warn;

            /** @brief Names the environment variable that sets the initial level. */
            constexpr const char* K_LOG_LEVEL_VARIABLE = "SR_LOG_LEVEL";

            /** @brief Pairs a level with its lower-case name and the label of its records. */
            struct LevelName
            {
                LogLevel level;    /**< The level. */
                const char* name;  /**< What SR_LOG_LEVEL calls it. */
                const char* label; /**< What a record's prefix calls it. */
            };

            /** @brief Lists every level with its two spellings. */
            constexpr LevelName K_LEVEL_NAMES[] = {
                {LogLevel::Off, "off", "OFF"},
                {LogLevel::Error, "error", "ERROR"},
                {LogLevel::Warn, "warn", "WARN"},
                {LogLevel::Info, "info", "INFO"},
                {LogLevel::Debug, "debug", "DEBUG"},
            };

            /** @brief Returns whether @p text equals @p lower_name when its case is ignored. */
            bool equals_ignoring_case(const char* text, const char* lower_name) noexcept
            {
                for (; *text != '\0' && *lower_name != '\0'; ++text, ++lower_name)
                {
                    if (std::tolower(static_cast<unsigned char>(*text)) != *lower_name)
                        return false;
                }
                return *text == '\0' && *lower_name == '\0';
            }

            /**
             * @brief Returns the level SR_LOG_LEVEL names, or the default.
             *
             * A value that names no level is reported on stderr and the default is returned.
             */
            int level_from_environment() noexcept
            {
                const char* value = std::getenv(K_LOG_LEVEL_VARIABLE);
                if (value == nullptr || *value == '\0')
                    return static_cast<int>(K_DEFAULT_LOG_LEVEL);

                for (const LevelName& entry : K_LEVEL_NAMES)
                {
                    if (equals_ignoring_case(value, entry.name))
                        return static_cast<int>(entry.level);
                }

                std::fprintf(stderr,
                             "[SushiRuntime] %s='%s' names no log level; expected off, error, "
                             "warn, info or debug. The level stays warn.\n",
                             K_LOG_LEVEL_VARIABLE, value);
                return static_cast<int>(K_DEFAULT_LOG_LEVEL);
            }

            /**
             * @brief Returns the process level, reading SR_LOG_LEVEL the first time unless
             *        set_log_level() ran earlier.
             */
            int resolve_log_level() noexcept
            {
                int current = Detail::active_log_level.load(std::memory_order_relaxed);
                if (current != Detail::K_LOG_LEVEL_UNRESOLVED)
                    return current;

                static const int from_environment = level_from_environment();
                if (Detail::active_log_level.compare_exchange_strong(
                        current, from_environment, std::memory_order_relaxed))
                    return from_environment;
                return current;
            }

            /** @brief Returns the label a record of @p level carries in its prefix. */
            const char* label_of(LogLevel level) noexcept
            {
                for (const LevelName& entry : K_LEVEL_NAMES)
                {
                    if (entry.level == level)
                        return entry.label;
                }
                return "LOG";
            }

            /** @brief Converts @p time to local calendar time without shared state. */
            std::tm local_time(std::time_t time) noexcept
            {
                std::tm result{};
                #ifdef _WIN32
                    localtime_s(&result, &time);
                #else
                    localtime_r(&time, &result);
                #endif
                return result;
            }

            /** @brief Appends the local time of day to @p out as HH:MM:SS.mmm. */
            void append_timestamp(std::string& out)
            {
                const auto now = std::chrono::system_clock::now();
                const std::tm calendar = local_time(std::chrono::system_clock::to_time_t(now));
                const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              now.time_since_epoch()) % 1000;

                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", calendar.tm_hour,
                              calendar.tm_min, calendar.tm_sec,
                              static_cast<int>(milliseconds.count()));
                out += buffer;
            }

            /** @brief Writes @p line and a line break to stderr in one call, then flushes. */
            void write_to_stderr(const std::string& line) noexcept
            {
                std::fprintf(stderr, "%s\n", line.c_str());
                std::fflush(stderr);
            }

            /**
             * @brief Owns the sink, the queue in front of it and the thread that empties it.
             * @see modules/foundation/logging/README.md
             */
            class LogWriter final
            {
                public:
                    LogWriter() = default;
                    LogWriter(const LogWriter&) = delete;
                    LogWriter& operator=(const LogWriter&) = delete;
                    LogWriter(LogWriter&&) = delete;
                    LogWriter& operator=(LogWriter&&) = delete;
                    ~LogWriter() = default;

                    /**
                     * @brief Queues the record, or delivers it on this thread in synchronous
                     *        mode and after shut_down().
                     */
                    void write(LogLevel level, std::string&& line)
                    {
                        bool queued = false;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            if (!synchronous_ && !stopping_ && is_worker_running())
                            {
                                queue_.push_back(Record{level, std::move(line)});
                                queued = true;
                            }
                        }

                        if (queued)
                        {
                            work_ready_.notify_one();
                            return;
                        }

                        // Once set, sink_abandoned_ stays set and sink_mutex_ is never released.
                        if (sink_abandoned_.load(std::memory_order_acquire))
                        {
                            write_to_stderr(line);
                            return;
                        }

                        std::lock_guard<std::mutex> lock(sink_mutex_);
                        deliver(level, line);
                    }

                    /**
                     * @brief Swaps the sink in once no call into the old one is running.
                     *
                     * Does nothing after shut_down() found sink_mutex_ abandoned.
                     */
                    void set_sink(LogSink sink)
                    {
                        // Once set, sink_abandoned_ stays set and sink_mutex_ is never released.
                        if (sink_abandoned_.load(std::memory_order_acquire))
                            return;

                        // The old sink leaves in the parameter and dies after the unlock.
                        std::lock_guard<std::mutex> lock(sink_mutex_);
                        std::swap(sink_, sink);
                    }

                    /** @brief Sets the mode; entering synchronous mode waits for the queue. */
                    void set_synchronous(bool synchronous)
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        synchronous_ = synchronous;
                        if (!synchronous)
                            return;

                        idle_.wait(lock, [this]()
                        {
                            return stopping_ || (queue_.empty() && !delivering_);
                        });
                    }

                    /**
                     * @brief Stops the thread and delivers what it left queued on this thread.
                     *
                     * Records are dropped when the sink's mutex is held, which happens only
                     * when the operating system ended the thread inside a sink call. That
                     * mutex is then recorded as abandoned, and no later call takes it.
                     */
                    void shut_down()
                    {
                        std::thread worker;
                        std::deque<Record> remaining;
                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            stopping_ = true;
                            worker = std::move(worker_);
                        }
                        work_ready_.notify_all();
                        idle_.notify_all();

                        if (worker.joinable())
                            worker.join();

                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            remaining.swap(queue_);
                        }

                        std::unique_lock<std::mutex> lock(sink_mutex_, std::try_to_lock);
                        if (!lock.owns_lock())
                        {
                            sink_abandoned_.store(true, std::memory_order_release);
                            return;
                        }

                        for (Record& record : remaining)
                            deliver(record.level, record.line);
                    }

                private:
                    /** @brief Holds one line on its way to the sink. */
                    struct Record
                    {
                        LogLevel level;   /**< The level the line was logged at. */
                        std::string line; /**< The text, without a line break. */
                    };

                    /**
                     * @brief Returns whether the thread exists, starting it on the first call.
                     * @pre queue_mutex_ is held.
                     */
                    bool is_worker_running()
                    {
                        if (worker_.joinable())
                            return true;

                        try
                        {
                            worker_ = std::thread(&LogWriter::run, this);
                            return true;
                        }
                        catch (const std::system_error& error)
                        {
                            std::fprintf(stderr,
                                         "[SushiRuntime] the log thread did not start (%s); "
                                         "records are delivered on the calling thread.\n",
                                         error.what());
                            synchronous_ = true;
                            return false;
                        }
                    }

                    /** @brief Delivers queued records until shut_down() finds the queue empty. */
                    void run()
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        while (true)
                        {
                            work_ready_.wait(lock, [this]()
                            {
                                return stopping_ || !queue_.empty();
                            });

                            if (queue_.empty())
                                return;

                            Record record = std::move(queue_.front());
                            queue_.pop_front();
                            delivering_ = true;
                            lock.unlock();

                            {
                                std::lock_guard<std::mutex> sink_lock(sink_mutex_);
                                deliver(record.level, record.line);
                            }

                            lock.lock();
                            delivering_ = false;
                            if (queue_.empty())
                                idle_.notify_all();
                        }
                    }

                    /**
                     * @brief Calls the sink, or writes to stderr when none is set or it throws.
                     * @pre sink_mutex_ is held.
                     */
                    void deliver(LogLevel level, const std::string& line) noexcept
                    {
                        if (!sink_)
                        {
                            write_to_stderr(line);
                            return;
                        }

                        try
                        {
                            sink_(level, std::string_view(line));
                            return;
                        }
                        catch (const std::exception& error)
                        {
                            std::fprintf(stderr,
                                         "[SushiRuntime] the log sink threw '%s'; its record "
                                         "follows on stderr.\n",
                                         error.what());
                        }
                        catch (...)
                        {
                            std::fprintf(stderr,
                                         "[SushiRuntime] the log sink threw a value that is not "
                                         "a std::exception; its record follows on stderr.\n");
                        }
                        write_to_stderr(line);
                    }

                    /// @brief Guards queue_, worker_, synchronous_, stopping_ and delivering_.
                    std::mutex queue_mutex_;
                    /// @brief Wakes the thread when a record arrives or shut_down() runs.
                    std::condition_variable work_ready_;
                    /// @brief Wakes set_synchronous() when the queue is empty and delivered.
                    std::condition_variable idle_;
                    /// @brief Records waiting for the thread, oldest first.
                    std::deque<Record> queue_;
                    /// @brief The thread that calls the sink in asynchronous mode.
                    std::thread worker_;
                    /// @brief True while each log call delivers its own record.
                    bool synchronous_ = false;
                    /// @brief True from shut_down() on; no record is queued after it.
                    bool stopping_ = false;
                    /// @brief True while the thread is inside deliver().
                    bool delivering_ = false;

                    /// @brief Guards sink_ and is held across every call into it.
                    std::mutex sink_mutex_;
                    /// @brief The sink of the process; empty means stderr.
                    LogSink sink_;
                    /// @brief Set once when shut_down() finds sink_mutex_ held; never cleared.
                    std::atomic<bool> sink_abandoned_{false};
            };

            /** @brief Shuts the writer it was given down when the library's statics die. */
            class LogWriterLifetime final
            {
                public:
                    explicit LogWriterLifetime(LogWriter& writer) noexcept : writer_(writer)
                    {

                    }

                    LogWriterLifetime(const LogWriterLifetime&) = delete;
                    LogWriterLifetime& operator=(const LogWriterLifetime&) = delete;
                    LogWriterLifetime(LogWriterLifetime&&) = delete;
                    LogWriterLifetime& operator=(LogWriterLifetime&&) = delete;

                    ~LogWriterLifetime()
                    {
                        writer_.shut_down();
                    }

                private:
                    /// @brief The writer to shut down.
                    LogWriter& writer_;
            };

            /** @brief Returns the writer of the process, created on the first call. */
            LogWriter& log_writer()
            {
                // The writer is never destroyed, so a log call is valid during process exit.
                static LogWriter* const writer = new LogWriter;
                static const LogWriterLifetime lifetime(*writer);
                return *writer;
            }
        } // namespace

        namespace Detail
        {
            std::atomic<int> active_log_level{K_LOG_LEVEL_UNRESOLVED};

            bool is_log_enabled(LogLevel level) noexcept
            {
                return static_cast<int>(level) <= resolve_log_level();
            }

            void emit_record(LogLevel level, const char* file, int line, const char* function,
                             std::string&& message)
            {
                std::string text;
                text.reserve(message.size() + 96);
                text += '[';
                append_timestamp(text);
                text += "] [";
                text += label_of(level);
                text += "] [";
                text += file;
                text += ':';
                text += std::to_string(line);
                text += "] [";
                text += function;
                text += "] ";
                text += message;
                log_write(level, std::move(text));
            }
        } // namespace Detail

        void set_log_level(LogLevel level) noexcept
        {
            Detail::active_log_level.store(static_cast<int>(level), std::memory_order_relaxed);
        }

        LogLevel log_level() noexcept
        {
            return static_cast<LogLevel>(resolve_log_level());
        }

        void set_log_sink(LogSink sink)
        {
            log_writer().set_sink(std::move(sink));
        }

        void log_write(LogLevel level, std::string&& line)
        {
            log_writer().write(level, std::move(line));
        }

        void set_log_synchronous(bool synchronous)
        {
            log_writer().set_synchronous(synchronous);
        }
    } // namespace Core
} // namespace SushiRuntime
