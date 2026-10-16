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
#include <array>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds the level of a category that nothing sets. */
        constexpr LogLevel K_DEFAULT_LOG_LEVEL = LogLevel::Warn;

        /** @brief Names the environment variable that sets the initial levels. */
        constexpr const char* K_LOG_LEVEL_VARIABLE = "SR_LOG_LEVEL";

        /** @brief Holds how many categories the level table keeps apart. */
        constexpr std::size_t K_MAX_LOG_CATEGORIES = 64;

        /** @brief Holds how many records wait for the log thread before one is dropped. */
        constexpr std::size_t K_LOG_QUEUE_CAPACITY = 4096;

        /** @brief Names the category of the records the logger writes about itself. */
        constexpr LogCategory K_LOGGING_LOG_CATEGORY = make_log_category("logging");

        /** @brief Pairs a level with its lower-case name and the label of its records. */
        struct LevelName
        {
            LogLevel level;    /**< The level. */
            const char* name;  /**< What SR_LOG_LEVEL calls it. */
            const char* label; /**< What a record's prefix calls it. */
        };

        /** @brief Lists every level with its two spellings. */
        constexpr LevelName K_LEVEL_NAMES[] = {
            {LogLevel::Trace, "trace", "TRACE"},
            {LogLevel::Debug, "debug", "DEBUG"},
            {LogLevel::Info, "info", "INFO"},
            {LogLevel::Warn, "warn", "WARN"},
            {LogLevel::Error, "error", "ERROR"},
            {LogLevel::Fatal, "fatal", "FATAL"},
            {LogLevel::Off, "off", "OFF"},
        };

        /** @brief Returns @p text without its leading and trailing spaces. */
        std::string_view trimmed(std::string_view text) noexcept
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                text.remove_suffix(1);
            return text;
        }

        /** @brief Returns whether @p text equals @p lower_name when its case is ignored. */
        bool equals_ignoring_case(std::string_view text, std::string_view lower_name) noexcept
        {
            if (text.size() != lower_name.size())
                return false;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(text[i])) != lower_name[i])
                    return false;
            }
            return true;
        }

        /** @brief Returns the level @p text names, ignoring case and spaces, or nothing. */
        std::optional<LogLevel> level_named(std::string_view text) noexcept
        {
            for (const LevelName& entry : K_LEVEL_NAMES)
            {
                if (equals_ignoring_case(trimmed(text), entry.name))
                    return entry.level;
            }
            return std::nullopt;
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

        /** @brief Returns @p text in lower case. */
        std::string to_lower(std::string_view text)
        {
            std::string out(text);
            for (char& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        /**
         * @brief Holds one atomic level per category, and the level a new category takes.
         *
         * Made on the first log call or level call of the process, when it reads SR_LOG_LEVEL.
         * A slot keeps its address for the life of the process.
         * @see modules/foundation/logging/README.md
         */
        class LevelTable final
        {
            public:
                LevelTable()
                {
                    read_environment();
                }

                LevelTable(const LevelTable&) = delete;
                LevelTable& operator=(const LevelTable&) = delete;
                LevelTable(LevelTable&&) = delete;
                LevelTable& operator=(LevelTable&&) = delete;
                ~LevelTable() = default;

                /** @brief Returns the slot of the category @p name, adding it on first use. */
                std::atomic<int>& slot(std::string_view name) noexcept
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return find_or_add(name).level;
                }

                /** @brief Sets the default and every category set_one() has not pinned. */
                void set_all(LogLevel level) noexcept
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    default_level_ = static_cast<int>(level);
                    for (std::size_t i = 0; i < count_; ++i)
                    {
                        if (!slots_[i].pinned)
                            slots_[i].level.store(default_level_, std::memory_order_relaxed);
                    }
                    overflow_.level.store(default_level_, std::memory_order_relaxed);
                }

                /** @brief Sets one category and pins it against set_all(). */
                void set_one(std::string_view name, LogLevel level) noexcept
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    Slot& entry = find_or_add(name);
                    entry.pinned = true;
                    entry.level.store(static_cast<int>(level), std::memory_order_relaxed);
                }

                /** @brief Returns the level a category takes when nothing sets its own. */
                LogLevel default_level() noexcept
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return static_cast<LogLevel>(default_level_);
                }

            private:
                /** @brief Holds the level of one category. */
                struct Slot
                {
                    std::string name;          /**< The category, or empty. */
                    std::atomic<int> level{0}; /**< Its level, as an int. */
                    bool pinned = false;       /**< True once set_one() named it. */
                };

                /** @brief Holds a level SR_LOG_LEVEL names for one category. */
                struct Override
                {
                    std::string name; /**< The category, in lower case. */
                    int level;        /**< Its level, as an int. */
                };

                /**
                 * @brief Returns the slot of @p name, adding it with its level from the
                 *        environment or the default; past K_MAX_LOG_CATEGORIES all share one.
                 * @pre mutex_ is held.
                 */
                Slot& find_or_add(std::string_view name) noexcept
                {
                    for (std::size_t i = 0; i < count_; ++i)
                    {
                        if (slots_[i].name == name)
                            return slots_[i];
                    }
                    if (count_ == K_MAX_LOG_CATEGORIES)
                    {
                        if (!overflow_reported_)
                        {
                            overflow_reported_ = true;
                            std::fprintf(stderr,
                                         "[SushiRuntime] more than %zu log categories; '%.*s' "
                                         "and every later one share the default level.\n",
                                         K_MAX_LOG_CATEGORIES, static_cast<int>(name.size()),
                                         name.data());
                        }
                        return overflow_;
                    }

                    Slot& entry = slots_[count_++];
                    entry.name.assign(name.data(), name.size());
                    int level = default_level_;
                    for (const Override& item : overrides_)
                    {
                        if (item.name == name)
                            level = item.level;
                    }
                    entry.level.store(level, std::memory_order_relaxed);
                    return entry;
                }

                /**
                 * @brief Reads SR_LOG_LEVEL: a level for every category, then category=level
                 *        pairs. A value it cannot read is reported once and changes nothing.
                 */
                void read_environment()
                {
                    overflow_.level.store(default_level_, std::memory_order_relaxed);
                    const char* value = std::getenv(K_LOG_LEVEL_VARIABLE);
                    if (value == nullptr || *value == '\0')
                        return;

                    int base = default_level_;
                    std::vector<Override> pairs;
                    std::string_view rest(value);
                    bool readable = true;
                    while (readable)
                    {
                        const std::size_t comma = rest.find(',');
                        const std::string_view item = trimmed(rest.substr(0, comma));
                        const std::size_t equals = item.find('=');
                        if (equals == std::string_view::npos)
                        {
                            const std::optional<LogLevel> level = level_named(item);
                            readable = level.has_value();
                            if (readable)
                                base = static_cast<int>(*level);
                        }
                        else
                        {
                            const std::string_view name = trimmed(item.substr(0, equals));
                            const std::optional<LogLevel> level =
                                level_named(item.substr(equals + 1));
                            readable = !name.empty() && level.has_value();
                            if (readable)
                                pairs.push_back(Override{to_lower(name), static_cast<int>(*level)});
                        }
                        if (comma == std::string_view::npos)
                            break;
                        rest.remove_prefix(comma + 1);
                    }

                    if (!readable)
                    {
                        std::fprintf(stderr,
                                     "[SushiRuntime] %s='%s' is not a level or a list of "
                                     "category=level pairs (trace, debug, info, warn, error, "
                                     "fatal, off). Every level stays warn.\n",
                                     K_LOG_LEVEL_VARIABLE, value);
                        return;
                    }
                    default_level_ = base;
                    overflow_.level.store(base, std::memory_order_relaxed);
                    overrides_ = std::move(pairs);
                }

                /// @brief Guards everything but the values of the atomics.
                std::mutex mutex_;
                /// @brief The categories, in the order of their first use.
                std::array<Slot, K_MAX_LOG_CATEGORIES> slots_;
                /// @brief How many entries of slots_ are in use.
                std::size_t count_ = 0;
                /// @brief The slot every category past K_MAX_LOG_CATEGORIES shares.
                Slot overflow_;
                /// @brief True once the overflow has been reported.
                bool overflow_reported_ = false;
                /// @brief The level a new category takes, as an int.
                int default_level_ = static_cast<int>(K_DEFAULT_LOG_LEVEL);
                /// @brief The category=level pairs SR_LOG_LEVEL named.
                std::vector<Override> overrides_;
        };

        /** @brief Returns the level table of the process, made on the first call. */
        LevelTable& level_table()
        {
            // Never destroyed, so a log call is valid during process exit.
            static LevelTable* const table = new LevelTable;
            return *table;
        }

        /** @brief Returns the number of the calling thread, given on its first record. */
        unsigned thread_number() noexcept
        {
            static std::atomic<unsigned> next{1};
            thread_local const unsigned number = next.fetch_add(1, std::memory_order_relaxed);
            return number;
        }

        /** @brief Returns the part of @p path after its last separator. */
        std::string_view file_name(const char* path) noexcept
        {
            const std::string_view text(path);
            const std::size_t slash = text.find_last_of("/\\");
            return slash == std::string_view::npos ? text : text.substr(slash + 1);
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
         * @brief Returns one record line: the time, the level, the category, the thread, the
         *        file and line of the call, then @p message.
         */
        std::string format_record(LogLevel level, std::string_view category, const char* file,
                                  int line, std::string_view message)
        {
            std::string text;
            text.reserve(message.size() + 96);
            text += '[';
            append_timestamp(text);
            text += "] [";
            text += label_of(level);
            text += "] [";
            text += category;
            text += "] [t";
            text += std::to_string(thread_number());
            text += "] [";
            text += file_name(file);
            text += ':';
            text += std::to_string(line);
            text += "] ";
            text += message;
            return text;
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
                 *        mode and after shut_down(). A record that finds the queue full is
                 *        dropped and counted; a FATAL one goes to write_fatal().
                 */
                void write(LogLevel level, std::string&& line)
                {
                    if (level == LogLevel::Fatal)
                    {
                        write_fatal(line);
                        return;
                    }

                    bool queued = false;
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        if (!synchronous_ && !stopping_ && is_worker_running())
                        {
                            if (queue_.size() >= K_LOG_QUEUE_CAPACITY)
                            {
                                ++dropped_;
                                return;
                            }
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

                    std::size_t dropped = 0;
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        remaining.swap(queue_);
                        dropped = std::exchange(dropped_, 0);
                    }

                    std::unique_lock<std::mutex> lock(sink_mutex_, std::try_to_lock);
                    if (!lock.owns_lock())
                    {
                        sink_abandoned_.store(true, std::memory_order_release);
                        return;
                    }

                    report_dropped(dropped);
                    for (Record& record : remaining)
                        deliver(record.level, record.line);
                }

            private:
                /**
                 * @brief Delivers a FATAL record on this thread after every queued one, then
                 *        flushes every output stream.
                 */
                void write_fatal(const std::string& line)
                {
                    std::size_t dropped = 0;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        idle_.wait(lock, [this]()
                        {
                            return stopping_ || !worker_.joinable() ||
                                   (queue_.empty() && !delivering_);
                        });
                        dropped = std::exchange(dropped_, 0);
                    }

                    // Once set, sink_abandoned_ stays set and sink_mutex_ is never released.
                    if (!sink_abandoned_.load(std::memory_order_acquire))
                    {
                        std::lock_guard<std::mutex> lock(sink_mutex_);
                        report_dropped(dropped);
                        deliver(LogLevel::Fatal, line);
                    }
                    else
                    {
                        write_to_stderr(line);
                    }
                    std::fflush(nullptr);
                }

                /**
                 * @brief Delivers a WARN that names how many records the full queue dropped.
                 * @pre sink_mutex_ is held.
                 */
                void report_dropped(std::size_t dropped) noexcept
                {
                    if (dropped == 0)
                        return;
                    try
                    {
                        const std::string message =
                            "dropped " + std::to_string(dropped) + " records: the queue of " +
                            std::to_string(K_LOG_QUEUE_CAPACITY) +
                            " was full; the records after them are delivered";
                        deliver(LogLevel::Warn,
                                format_record(LogLevel::Warn, K_LOGGING_LOG_CATEGORY.name(),
                                              __FILE__, __LINE__, message));
                    }
                    catch (const std::exception& error)
                    {
                        std::fprintf(stderr,
                                     "[SushiRuntime] %zu log records were dropped, and the "
                                     "report of it failed: %s\n",
                                     dropped, error.what());
                    }
                }

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
                        const std::size_t dropped = std::exchange(dropped_, 0);
                        delivering_ = true;
                        lock.unlock();

                        {
                            std::lock_guard<std::mutex> sink_lock(sink_mutex_);
                            report_dropped(dropped);
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

                /// @brief Guards queue_, worker_, synchronous_, stopping_, delivering_, dropped_.
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
                /// @brief Records dropped since the last delivery because the queue was full.
                std::size_t dropped_ = 0;

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
        std::atomic<int>& log_level_slot(LogCategory category) noexcept
        {
            return level_table().slot(category.name());
        }

        void emit_record(LogLevel level, LogCategory category, const char* file, int line,
                         std::string&& message)
        {
            log_write(level, format_record(level, category.name(), file, line, message));
        }
    } // namespace Detail

    void set_log_level(LogLevel level) noexcept
    {
        level_table().set_all(level);
    }

    LogLevel log_level() noexcept
    {
        return level_table().default_level();
    }

    void set_log_level(LogCategory category, LogLevel level) noexcept
    {
        level_table().set_one(category.name(), level);
    }

    LogLevel log_level(LogCategory category) noexcept
    {
        return static_cast<LogLevel>(
            Detail::log_level_slot(category).load(std::memory_order_relaxed));
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
} // namespace SushiRuntime
