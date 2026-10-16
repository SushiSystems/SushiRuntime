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
 * @brief Implements the levels of each project and category, the sink and its thread.
 * @author Mustafa Garip
 * @see modules/foundation/logging/README.md
 */

// The Microsoft C runtime marks std::getenv deprecated unless this is defined first.
#define _CRT_SECURE_NO_WARNINGS

#include <SushiRuntime/logging/logger.hpp>

#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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

#include <SushiRuntime/log/log_c_sink.h>

namespace SushiRuntime
{
    namespace
    {
        /** @brief Holds the level of a category that nothing sets. */
        constexpr LogLevel K_DEFAULT_LOG_LEVEL = LogLevel::Warn;

        /** @brief Holds how many categories the level table keeps apart. */
        constexpr std::size_t K_MAX_LOG_CATEGORIES = 64;

        /** @brief Holds how many records wait for the log thread before one is dropped. */
        constexpr std::size_t K_LOG_QUEUE_CAPACITY = 4096;

        /** @brief Names the category of the records the logger writes about itself. */
        constexpr LogCategory K_LOGGING_LOG_CATEGORY =
            make_log_category(K_SUSHIRUNTIME_LOG_PROJECT, "logging");

        /** @brief Pairs a level with its lower-case name and the label of its records. */
        struct LevelName
        {
            LogLevel level;    /**< The level. */
            const char* name;  /**< What a level variable calls it. */
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

        /** @brief Holds a level a project's variable names for one of its modules. */
        struct ModuleLevel
        {
            std::string module; /**< The module, in lower case. */
            int level;          /**< Its level, as an int. */
        };

        /** @brief Holds what a project's level variable says. */
        struct LevelSetting
        {
            int level;                        /**< The level of every module, as an int. */
            std::vector<ModuleLevel> modules; /**< The module=level pairs. */
        };

        /**
         * @brief Reads @p value: a level for every module, then module=level pairs.
         * @return Nothing when an item is neither.
         */
        std::optional<LevelSetting> read_level_setting(std::string_view value)
        {
            LevelSetting setting{static_cast<int>(K_DEFAULT_LOG_LEVEL), {}};
            std::string_view rest = value;
            while (true)
            {
                const std::size_t comma = rest.find(',');
                const std::string_view item = trimmed(rest.substr(0, comma));
                const std::size_t equals = item.find('=');
                if (equals == std::string_view::npos)
                {
                    const std::optional<LogLevel> level = level_named(item);
                    if (!level)
                        return std::nullopt;
                    setting.level = static_cast<int>(*level);
                }
                else
                {
                    const std::string_view module = trimmed(item.substr(0, equals));
                    const std::optional<LogLevel> level = level_named(item.substr(equals + 1));
                    if (module.empty() || !level)
                        return std::nullopt;
                    setting.modules.push_back(ModuleLevel{to_lower(module),
                                                          static_cast<int>(*level)});
                }
                if (comma == std::string_view::npos)
                    return setting;
                rest.remove_prefix(comma + 1);
            }
        }

        /**
         * @brief Holds one atomic level per category and the level of each project.
         *
         * A project is added on the first use of one of its categories, when its level
         * variable is read. A slot keeps its address for the life of the process.
         * @see modules/foundation/logging/README.md
         */
        class LevelTable final
        {
            public:
                LevelTable()
                {
                    overflow_.level.store(static_cast<int>(K_DEFAULT_LOG_LEVEL),
                                          std::memory_order_relaxed);
                }

                LevelTable(const LevelTable&) = delete;
                LevelTable& operator=(const LevelTable&) = delete;
                LevelTable(LevelTable&&) = delete;
                LevelTable& operator=(LevelTable&&) = delete;
                ~LevelTable() = default;

                /** @brief Returns the slot of @p category, adding it on first use. */
                std::atomic<int>& slot(LogCategory category)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return find_or_add(category).level;
                }

                /** @brief Sets @p project and every category of it set_one() has not pinned. */
                void set_project(LogProject project, LogLevel level)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const std::size_t index = project_index(project);
                    projects_[index].level = static_cast<int>(level);
                    for (std::size_t i = 0; i < count_; ++i)
                    {
                        if (slots_[i].project == index && !slots_[i].pinned)
                            slots_[i].level.store(static_cast<int>(level),
                                                  std::memory_order_relaxed);
                    }
                }

                /** @brief Sets one category and pins it against set_project(). */
                void set_one(LogCategory category, LogLevel level)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    Slot& entry = find_or_add(category);
                    entry.pinned = true;
                    entry.level.store(static_cast<int>(level), std::memory_order_relaxed);
                }

                /** @brief Returns the level of @p project's categories that nothing pins. */
                LogLevel project_level(LogProject project)
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    return static_cast<LogLevel>(projects_[project_index(project)].level);
                }

            private:
                /** @brief Holds one project and what its variable said. */
                struct Project
                {
                    std::string name;                 /**< The project name. */
                    int level;                        /**< Its level, as an int. */
                    std::vector<ModuleLevel> modules; /**< The module=level pairs. */
                };

                /** @brief Holds the level of one category. */
                struct Slot
                {
                    std::size_t project = 0;   /**< The index of its project. */
                    std::string module;        /**< The module, or empty. */
                    std::atomic<int> level{0}; /**< Its level, as an int. */
                    bool pinned = false;       /**< True once set_one() named it. */
                };

                /**
                 * @brief Returns the index of @p project, adding it with the levels its
                 *        variable names.
                 * @pre mutex_ is held.
                 */
                std::size_t project_index(LogProject project)
                {
                    for (std::size_t i = 0; i < projects_.size(); ++i)
                    {
                        if (projects_[i].name == project.name())
                            return i;
                    }

                    const LevelSetting setting = read_variable(project);
                    projects_.push_back(Project{std::string(project.name()), setting.level,
                                                setting.modules});
                    return projects_.size() - 1;
                }

                /**
                 * @brief Returns the slot of @p category, adding it with its level from its
                 *        project; past K_MAX_LOG_CATEGORIES all share one.
                 * @pre mutex_ is held.
                 */
                Slot& find_or_add(LogCategory category)
                {
                    const std::size_t index = project_index(category.project());
                    for (std::size_t i = 0; i < count_; ++i)
                    {
                        if (slots_[i].project == index && slots_[i].module == category.module())
                            return slots_[i];
                    }

                    const Project& project = projects_[index];
                    if (count_ == K_MAX_LOG_CATEGORIES)
                    {
                        if (!overflow_reported_)
                        {
                            overflow_reported_ = true;
                            std::fprintf(stderr,
                                         "[SushiRuntime] more than %zu log categories; "
                                         "'%s.%.*s' and every later one share one level, %s.\n",
                                         K_MAX_LOG_CATEGORIES, project.name.c_str(),
                                         static_cast<int>(category.module().size()),
                                         category.module().data(),
                                         label_of(static_cast<LogLevel>(project.level)));
                            overflow_.level.store(project.level, std::memory_order_relaxed);
                        }
                        return overflow_;
                    }

                    Slot& entry = slots_[count_++];
                    entry.project = index;
                    entry.module.assign(category.module().data(), category.module().size());
                    int level = project.level;
                    for (const ModuleLevel& item : project.modules)
                    {
                        if (item.module == entry.module)
                            level = item.level;
                    }
                    entry.level.store(level, std::memory_order_relaxed);
                    return entry;
                }

                /**
                 * @brief Reads @p project's level variable; a value it cannot read is reported
                 *        once and leaves every level of the project at Warn.
                 */
                static LevelSetting read_variable(LogProject project)
                {
                    const LevelSetting fallback{static_cast<int>(K_DEFAULT_LOG_LEVEL), {}};
                    const std::string variable(project.level_variable());
                    const char* value = std::getenv(variable.c_str());
                    if (value == nullptr || *value == '\0')
                        return fallback;

                    std::optional<LevelSetting> setting = read_level_setting(value);
                    if (setting)
                        return std::move(*setting);

                    std::fprintf(stderr,
                                 "[SushiRuntime] %s='%s' is not a level or a list of "
                                 "module=level pairs (trace, debug, info, warn, error, fatal, "
                                 "off). Every level of %.*s stays warn.\n",
                                 variable.c_str(), value,
                                 static_cast<int>(project.name().size()), project.name().data());
                    return fallback;
                }

                /// @brief Guards everything but the values of the atomics.
                std::mutex mutex_;
                /// @brief The projects, in the order of their first use.
                std::vector<Project> projects_;
                /// @brief The categories, in the order of their first use.
                std::array<Slot, K_MAX_LOG_CATEGORIES> slots_;
                /// @brief How many entries of slots_ are in use.
                std::size_t count_ = 0;
                /// @brief The slot every category past K_MAX_LOG_CATEGORIES shares.
                Slot overflow_;
                /// @brief True once the overflow has been reported.
                bool overflow_reported_ = false;
        };

        /** @brief Returns the level table of the process, made on the first call. */
        LevelTable& level_table()
        {
            // Never destroyed, so a log call is valid during process exit.
            static LevelTable* const table = new LevelTable;
            return *table;
        }

        /** @brief Returns the number of the calling thread, given on its first record. */
        std::uint32_t thread_number() noexcept
        {
            static std::atomic<std::uint32_t> next{1};
            thread_local const std::uint32_t number =
                next.fetch_add(1, std::memory_order_relaxed);
            return number;
        }

        /** @brief Returns the part of @p path after its last separator. */
        std::string_view file_name(std::string_view path) noexcept
        {
            const std::size_t slash = path.find_last_of("/\\");
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }

        /** @brief Returns the system clock time now, in nanoseconds since the Unix epoch. */
        std::int64_t now_ns() noexcept
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
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

        /** @brief Appends the local time of day of @p time_ns to @p out as HH:MM:SS.mmm. */
        void append_timestamp(std::string& out, std::int64_t time_ns)
        {
            constexpr std::int64_t K_NS_PER_SECOND = 1000000000;
            std::int64_t seconds = time_ns / K_NS_PER_SECOND;
            std::int64_t rest = time_ns % K_NS_PER_SECOND;
            if (rest < 0)
            {
                --seconds;
                rest += K_NS_PER_SECOND;
            }
            const std::tm calendar = local_time(static_cast<std::time_t>(seconds));

            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", calendar.tm_hour,
                          calendar.tm_min, calendar.tm_sec,
                          static_cast<int>(rest / 1000000));
            out += buffer;
        }

        /** @brief Writes @p line and a line break to stderr in one call, then flushes. */
        void write_to_stderr(const std::string& line) noexcept
        {
            std::fprintf(stderr, "%s\n", line.c_str());
            std::fflush(stderr);
        }

        /**
         * @brief Holds one record on its way to the sink, owning every string the sink's
         *        LogRecord views.
         */
        struct QueuedRecord
        {
            LogLevel level;      /**< The level the record was logged at. */
            std::string project; /**< The project of its category. */
            std::string module;  /**< The module of its category. */
            std::string file;    /**< The file of the call, without its folders. */
            std::uint32_t line;  /**< The line of the call. */
            std::uint32_t thread; /**< The logger's number for the calling thread. */
            std::int64_t time_ns; /**< The time of the call. */
            std::string message; /**< The formatted message. */
            std::string text;    /**< The whole line. */

            /** @brief Returns the views a sink receives; valid while this record lives. */
            [[nodiscard]] LogRecord view() const noexcept
            {
                return LogRecord{level, project, module, file, line, thread, time_ns,
                                 message, text};
            }
        };

        /**
         * @brief Returns a record of the calling thread with its line written: the time, the
         *        level, the project and module, the thread, the file and line, the message.
         */
        QueuedRecord make_record(LogLevel level, std::string_view project,
                                 std::string_view module, std::string_view file,
                                 std::uint32_t line, std::int64_t time_ns, std::string&& message)
        {
            QueuedRecord record{level,
                                std::string(project),
                                std::string(module),
                                std::string(file_name(file)),
                                line,
                                thread_number(),
                                time_ns,
                                std::move(message),
                                {}};

            std::string& text = record.text;
            text.reserve(record.message.size() + record.project.size() + record.module.size() +
                         record.file.size() + 48);
            text += '[';
            append_timestamp(text, time_ns);
            text += "] [";
            text += label_of(level);
            text += "] [";
            text += record.project;
            text += '.';
            text += record.module;
            text += "] [t";
            text += std::to_string(record.thread);
            text += "] [";
            text += record.file;
            text += ':';
            text += std::to_string(line);
            text += "] ";
            text += record.message;
            return record;
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
                void write(QueuedRecord&& record)
                {
                    if (record.level == LogLevel::Fatal)
                    {
                        write_fatal(record);
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
                            queue_.push_back(std::move(record));
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
                        write_to_stderr(record.text);
                        return;
                    }

                    std::lock_guard<std::mutex> lock(sink_mutex_);
                    deliver(record);
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
                    std::deque<QueuedRecord> remaining;
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
                    for (const QueuedRecord& record : remaining)
                        deliver(record);
                }

            private:
                /**
                 * @brief Delivers a FATAL record on this thread after every queued one, then
                 *        flushes every output stream.
                 */
                void write_fatal(const QueuedRecord& record)
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
                        deliver(record);
                    }
                    else
                    {
                        write_to_stderr(record.text);
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
                        std::string message =
                            "dropped " + std::to_string(dropped) + " records: the queue of " +
                            std::to_string(K_LOG_QUEUE_CAPACITY) +
                            " was full; the records after them are delivered";
                        deliver(make_record(LogLevel::Warn,
                                            K_LOGGING_LOG_CATEGORY.project().name(),
                                            K_LOGGING_LOG_CATEGORY.module(), __FILE__, __LINE__,
                                            now_ns(), std::move(message)));
                    }
                    catch (const std::exception& error)
                    {
                        std::fprintf(stderr,
                                     "[SushiRuntime] %zu log records were dropped, and the "
                                     "report of it failed: %s\n",
                                     dropped, error.what());
                    }
                }

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

                        QueuedRecord record = std::move(queue_.front());
                        queue_.pop_front();
                        const std::size_t dropped = std::exchange(dropped_, 0);
                        delivering_ = true;
                        lock.unlock();

                        {
                            std::lock_guard<std::mutex> sink_lock(sink_mutex_);
                            report_dropped(dropped);
                            deliver(record);
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
                void deliver(const QueuedRecord& record) noexcept
                {
                    if (!sink_)
                    {
                        write_to_stderr(record.text);
                        return;
                    }

                    try
                    {
                        sink_(record.view());
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
                    write_to_stderr(record.text);
                }

                /// @brief Guards queue_, worker_, synchronous_, stopping_, delivering_, dropped_.
                std::mutex queue_mutex_;
                /// @brief Wakes the thread when a record arrives or shut_down() runs.
                std::condition_variable work_ready_;
                /// @brief Wakes set_synchronous() when the queue is empty and delivered.
                std::condition_variable idle_;
                /// @brief Records waiting for the thread, oldest first.
                std::deque<QueuedRecord> queue_;
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

        /** @brief Enters a record another logger made into this logger's queue. */
        void forward_record(const LogRecord& record)
        {
            log_writer().write(make_record(record.level, record.project, record.module,
                                           record.file, record.line, record.time_ns,
                                           std::string(record.message)));
        }

        /** @brief Returns @p record in the C layout of log_c_sink.h. */
        SushiLogRecord to_c_record(const LogRecord& record) noexcept
        {
            SushiLogRecord out{};
            out.size = static_cast<std::uint32_t>(sizeof(SushiLogRecord));
            out.level = static_cast<std::uint8_t>(record.level);
            out.project = record.project.data();
            out.project_size = record.project.size();
            out.module = record.module.data();
            out.module_size = record.module.size();
            out.file = record.file.data();
            out.file_size = record.file.size();
            out.line = record.line;
            out.thread = record.thread;
            out.time_ns = record.time_ns;
            out.message = record.message.data();
            out.message_size = record.message.size();
            out.text = record.text.data();
            out.text_size = record.text.size();
            return out;
        }
    } // namespace

    namespace Detail
    {
        std::atomic<int>& log_level_slot(LogCategory category) noexcept
        {
            return level_table().slot(category);
        }

        void emit_record(LogLevel level, LogCategory category, const char* file, int line,
                         std::string&& message)
        {
            log_writer().write(make_record(level, category.project().name(), category.module(),
                                           file, static_cast<std::uint32_t>(line), now_ns(),
                                           std::move(message)));
        }
    } // namespace Detail

    void set_log_level(LogProject project, LogLevel level) noexcept
    {
        level_table().set_project(project, level);
    }

    LogLevel log_level(LogProject project) noexcept
    {
        return level_table().project_level(project);
    }

    void set_log_level(LogCategory category, LogLevel level) noexcept
    {
        level_table().set_one(category, level);
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

    void set_log_synchronous(bool synchronous)
    {
        log_writer().set_synchronous(synchronous);
    }

    LogSink log_forward_sink()
    {
        return LogSink([](const LogRecord& record)
        {
            forward_record(record);
        });
    }
} // namespace SushiRuntime

extern "C" void sr_set_log_sink(SushiLogSinkFunction function, void* user)
{
    try
    {
        if (function == nullptr)
        {
            SushiRuntime::set_log_sink(SushiRuntime::LogSink());
            return;
        }
        SushiRuntime::set_log_sink(
            SushiRuntime::LogSink([function, user](const SushiRuntime::LogRecord& record)
            {
                const SushiLogRecord c_record = SushiRuntime::to_c_record(record);
                function(&c_record, user);
            }));
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[SushiRuntime] sr_set_log_sink failed: %s; the sink is unchanged.\n",
                     error.what());
    }
}

extern "C" void sr_set_log_synchronous(int synchronous)
{
    try
    {
        SushiRuntime::set_log_synchronous(synchronous != 0);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr,
                     "[SushiRuntime] sr_set_log_synchronous failed: %s; the mode is unchanged.\n",
                     error.what());
    }
}
