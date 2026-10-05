/****************************************************************/
/* logger.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstdio>
#include <mutex>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <utility>
#include <string>

#ifndef SR_ENABLE_LOGGING
    #define SR_ENABLE_LOGGING 1
#endif

namespace SushiRuntime 
{
    namespace Core
    {
        /**
         * @brief Logging levels to filter messages.
         */
        enum class LogType : int 
        {
            OFF   = 0, /**< Logging is disabled */
            ERR   = 1, /**< Only error messages */
            WARN  = 2, /**< Warnings and errors */
            INFO  = 3, /**< Info, warnings and errors */
            DEBUG = 4  /**< All messages including debug */
        };

        #if SR_ENABLE_LOGGING
            #ifdef NDEBUG
                inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::INFO;
            #else
                inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::DEBUG;
            #endif
        #else
            inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::OFF;
        #endif

        /**
         * @brief Internal tools for the logging system.
         */
        namespace Detail 
        {
            /**
             * @brief Asynchronous log writer with worker thread.
             *
             * Shutdown protocol (race-free):
             *   1. Destructor acquires the mutex.
             *   2. Sets running_ = false WHILE HOLDING the mutex.
             *   3. Releases the mutex and notifies the condvar.
             *   4. Joins the worker.
             *
             * Without holding the mutex during the store, the worker could
             * evaluate its wait-predicate (queue empty AND running_ == true),
             * decide to sleep, and then park on the condvar AFTER the
             * destructor's notify_one fires. The notify is then lost and the
             * worker sleeps forever, deadlocking the join. Holding the mutex
             * during the store closes that window: the worker is either
             * still inside the predicate (will re-check and exit), or
             * already parked (will be woken by the post-unlock notify).
             */
            class AsyncLogger 
            {
                private:
                    std::queue<std::pair<LogType, std::string>> queue_;
                    std::mutex mutex_;
                    std::condition_variable cv_;
                    std::thread worker_;
                    bool running_;  // Guarded by mutex_. Was atomic; the atomic
                                    // gave a false sense of safety because the
                                    // store/notify pair was not synchronized
                                    // with the worker's wait predicate.

                    void process_logs() 
                    {
                        while (true) 
                        {
                            std::unique_lock<std::mutex> lock(mutex_);
                            cv_.wait(lock, [this]() 
                            { 
                                return !queue_.empty() || !running_; 
                            });

                            // Drain the queue regardless of running_ state, so
                            // that messages enqueued just before shutdown are
                            // still flushed. Only exit once the queue is empty
                            // AND shutdown has been requested.
                            while (!queue_.empty()) 
                            {
                                auto log_entry = std::move(queue_.front());
                                queue_.pop();
                                lock.unlock();

                                FILE* stream = (log_entry.first <= LogType::ERR) ? stderr : stdout;

                                #ifdef _WIN32
                                    _lock_file(stream);
                                    fputs(log_entry.second.c_str(), stream);
                                    fflush(stream);
                                    _unlock_file(stream);
                                #else
                                    flockfile(stream);
                                    fputs(log_entry.second.c_str(), stream);
                                    fflush(stream);
                                    funlockfile(stream);
                                #endif
                                lock.lock();
                            }

                            if (!running_) return;
                        }
                    }

                public:
                    AsyncLogger() : running_(true) 
                    {
                        worker_ = std::thread(&AsyncLogger::process_logs, this);
                    }

                    ~AsyncLogger() 
                    {
                        // Set the shutdown flag while holding the mutex. This
                        // guarantees that when we notify below, any worker
                        // currently inside cv_.wait is either already parked
                        // (and the notify will wake it) or has not yet re-
                        // evaluated the predicate (and will see running_ ==
                        // false on the next check). Without the lock, the
                        // store could land between the predicate check and
                        // the park, and the notify would be lost.
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            running_ = false;
                        }
                        cv_.notify_one();

                        if (worker_.joinable()) 
                        {
                            worker_.join();
                        }
                    }

                    void enqueue(LogType level, std::string&& msg) 
                    {
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            queue_.emplace(level, std::move(msg));
                        }

                        cv_.notify_one();
                    }
            };

            /**
             * @brief Synchronous logger; writes each message immediately under a mutex.
             *
             * Used in test mode (SR_LOGGER_SET_SYNC_MODE(true)) where deterministic,
             * ordered output matters more than throughput. Errors go to stderr; all
             * other levels go to stdout.
             */
            class SyncLogger
            {
                private:
                    std::mutex sync_mutex_;
                public:
                    /**
                     * @brief Writes @p msg to the appropriate stream, protected by a mutex.
                     * @param level Log level; ERR and below go to stderr, others to stdout.
                     * @param msg   Formatted log line to write.
                     */
                    void enqueue(LogType level, std::string&& msg)
                    {
                        std::lock_guard<std::mutex> lock(sync_mutex_);
                        FILE* stream = (level <= LogType::ERR) ? stderr : stdout;
                        #ifdef _WIN32
                            _lock_file(stream);
                            fputs(msg.c_str(), stream);
                            fflush(stream);
                            _unlock_file(stream);
                        #else
                            flockfile(stream);
                            fputs(msg.c_str(), stream);
                            fflush(stream);
                            funlockfile(stream);
                        #endif
                    }
            };

            inline std::atomic<bool>& is_sync_logging_enabled()
            {
                static std::atomic<bool> mode{false};
                return mode;
            }

            /**
             * @brief Routes a log message to either the sync or async logger.
             *
             * The choice is governed by is_sync_logging_enabled(); switching modes
             * mid-run is safe because the flag is atomic and each logger owns its
             * own mutex / queue.
             *
             * @param level Severity of the message.
             * @param msg   Fully formatted log line (including timestamp and context).
             */
            inline void dispatch_log(LogType level, std::string&& msg)
            {
                if (is_sync_logging_enabled().load(std::memory_order_relaxed)) 
                {
                    static SyncLogger sync_logger;
                    sync_logger.enqueue(level, std::move(msg));
                } 
                else 
                {
                    static AsyncLogger async_logger;
                    async_logger.enqueue(level, std::move(msg));
                }
            }

            /**
             * @brief Gets local time in a platform-independent way.
             * @param result The tm structure to fill.
             * @param time The timestamp to convert.
             */
            inline void portable_localtime(std::tm* result, const std::time_t* time)
            {
                #ifdef _WIN32
                    localtime_s(result, time);
                #else
                    localtime_r(time, result);
                #endif
            }

            /**
             * @brief Creates a timestamp with millisecond precision.
             * @return Formatted time string (HH:MM:SS.mmm).
             */
            inline std::string get_timestamp()
            {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm;
                portable_localtime(&tm, &time);

                // add milliseconds for better tracking in async tasks
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                              tm.tm_hour, tm.tm_min, tm.tm_sec,
                              static_cast<int>(ms.count()));
                return std::string(buf);
            }

            /**
             * @brief Substitutes each {} placeholder with the next argument.
             *
             * A small std::format stand-in for C++17: it replaces positional {}
             * markers left to right by streaming each argument with operator<<,
             * and treats {{ and }} as literal braces. It intentionally supports no
             * format specs ({:...}) — call sites needing width/hex pre-format the
             * value before logging. Extra arguments and extra placeholders are
             * ignored rather than diagnosed, mirroring a logger's best-effort role.
             *
             * @param fmt The format string holding {} markers.
             * @return The fully substituted string (no trailing arguments left).
             */
            inline std::string sr_format(std::string_view fmt)
            {
                std::string out;
                out.reserve(fmt.size());
                for (std::size_t i = 0; i < fmt.size(); ++i)
                {
                    char c = fmt[i];
                    if (c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') { out += '{'; ++i; }
                    else if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') { out += '}'; ++i; }
                    else out += c;
                }
                return out;
            }

            template<typename T, typename... Args>
            std::string sr_format(std::string_view fmt, const T& value, const Args&... args)
            {
                std::string out;
                for (std::size_t i = 0; i < fmt.size(); ++i)
                {
                    char c = fmt[i];
                    if (c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '{') { out += '{'; ++i; continue; }
                    if (c == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}') { out += '}'; ++i; continue; }
                    if (c == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}')
                    {
                        std::ostringstream os;
                        os << value;
                        out += os.str();
                        out += sr_format(fmt.substr(i + 2), args...);
                        return out;
                    }
                    out += c;
                }
                return out;
            }

            template<typename... Args>
            void log_base(LogType level, std::string_view prefix,
                          const char* file, int line, const char* func,
                          std::string_view fmt, const Args&... args)
            {
                #if SR_ENABLE_LOGGING
                    std::string msg = sr_format(fmt, args...);
                    std::ostringstream os;
                    os << '[' << get_timestamp() << "] ["
                       << prefix << "] ["
                       << file << ':' << line << "] ["
                       << func << "] " << msg << '\n';
                    dispatch_log(level, os.str());
                #endif
            }
        } // namespace Detail
    } // namespace Core
    
    /**
     * @brief Logs a message at error level.
     */
    // The format string is folded into __VA_ARGS__ (so the macros take (...)
    // rather than (fmt, ...)): this keeps a single, always-present variadic list
    // and avoids __VA_OPT__/##__VA_ARGS__, neither of which is standard C++17.
    // log_base's std::string_view fmt parameter then binds the first argument.
    #if SR_ENABLE_LOGGING
        #define SR_LOG_ERROR(...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::ERR) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::ERR, "ERROR", __FILE__, __LINE__, __func__, __VA_ARGS__)

        /** @brief Logs a message at warning level. */
        #define SR_LOG_WARN(...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::WARN) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::WARN, "WARN", __FILE__, __LINE__, __func__, __VA_ARGS__)

        /** @brief Logs a message at info level. */
        #define SR_LOG_INFO(...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::INFO) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::INFO, "INFO", __FILE__, __LINE__, __func__, __VA_ARGS__)

        /** @brief Logs a message at debug level. */
        #define SR_LOG_DEBUG(...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::DEBUG) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::DEBUG, "DEBUG", __FILE__, __LINE__, __func__, __VA_ARGS__)
    #else
        #define SR_LOG_ERROR(...) ((void)0)
        #define SR_LOG_WARN(...)  ((void)0)
        #define SR_LOG_INFO(...)  ((void)0)
        #define SR_LOG_DEBUG(...) ((void)0)
    #endif

    /** @brief Switches the logger to synchronous block-printing mode (useful for tests). */
    #define SR_LOGGER_SET_SYNC_MODE(mode) \
        SushiRuntime::Core::Detail::is_sync_logging_enabled().store(mode, std::memory_order_release)

    /**
     * @brief Throws an exception and logs it if a condition is met.
     * @param condition The condition to check.
     * @param fmt The error message format.
     * @param ... The format arguments.
     */
    #define SR_THROW_IF(condition, ...)                                             \
        do {                                                                        \
            if (condition) {                                                        \
                std::string msg_ = SushiRuntime::Core::Detail::sr_format(__VA_ARGS__); \
                SR_LOG_ERROR("Runtime exception: {}", msg_);                        \
                throw std::runtime_error(msg_);                                     \
            }                                                                       \
        } while (0)

} // namespace SushiRuntime