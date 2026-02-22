/**************************************************************************/
/* logger.hpp                                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#pragma once

#include <cstdio>
#include <mutex>
#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <source_location>
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
            class AsyncLogger 
            {
                private:
                    std::queue<std::pair<LogType, std::string>> queue_;
                    std::mutex mutex_;
                    std::condition_variable cv_;
                    std::thread worker_;
                    std::atomic<bool> running_;

                    void process_logs() 
                    {
                        while (running_.load(std::memory_order_relaxed)) 
                        {
                            std::unique_lock<std::mutex> lock(mutex_);
                            cv_.wait(lock, [this]() { return !queue_.empty() || !running_.load(std::memory_order_relaxed); });

                            while (!queue_.empty()) 
                            {
                                auto log_entry = std::move(queue_.front());
                                queue_.pop();
                                lock.unlock(); // Unlock while writing to file/terminal

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
                                lock.lock(); // Lock again before checking queue
                            }
                        }
                    }

                public:
                    AsyncLogger() : running_(true) 
                    {
                        worker_ = std::thread(&AsyncLogger::process_logs, this);
                    }

                    ~AsyncLogger() 
                    {
                        running_.store(false, std::memory_order_relaxed);
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

            class SyncLogger 
            {
                private:
                    std::mutex sync_mutex_;
                public:
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
                return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count());
            }

            /**
             * @brief Extracts or formats the function name.
             * @param sv The string view to process.
             * @return The processed function name.
             */
            constexpr std::string_view extract_function_name(std::string_view sv)
            {
                return sv;
            }

            template<typename... Args>
            void log_base(LogType level, std::string_view prefix, const std::source_location& loc, std::string_view fmt, Args&&... args) 
            {
                #if SR_ENABLE_LOGGING
                    try 
                    {
                        std::string msg = std::vformat(fmt, std::make_format_args(args...));
                        std::string_view func_name = extract_function_name(loc.function_name());
                        
                        auto output = std::format("[{}] [{}] [{}:{}] [{}] {}\n", 
                            get_timestamp(), 
                            prefix, 
                            loc.file_name(),
                            loc.line(),
                            func_name, 
                            msg);
                        
                        dispatch_log(level, std::move(output));
                    } 
                    catch (const std::format_error& e) 
                    {
                        std::cerr << "Log format error: " << e.what() << "\n";
                    }
                #endif
            }
        } // namespace Detail
    } // namespace Core
    
    /**
     * @brief Logs a message at error level.
     */
    #if SR_ENABLE_LOGGING
        #define SR_LOG_ERROR(fmt, ...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::ERR) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::ERR, "ERROR", std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)

        /** @brief Logs a message at warning level. */
        #define SR_LOG_WARN(fmt, ...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::WARN) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::WARN, "WARN", std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)

        /** @brief Logs a message at info level. */
        #define SR_LOG_INFO(fmt, ...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::INFO) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::INFO, "INFO", std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)

        /** @brief Logs a message at debug level. */
        #define SR_LOG_DEBUG(fmt, ...) \
            if constexpr (SushiRuntime::Core::ACTIVE_LOG_LEVEL >= SushiRuntime::Core::LogType::DEBUG) \
                SushiRuntime::Core::Detail::log_base(SushiRuntime::Core::LogType::DEBUG, "DEBUG", std::source_location::current(), fmt __VA_OPT__(,) __VA_ARGS__)
    #else
        #define SR_LOG_ERROR(fmt, ...) ((void)0)
        #define SR_LOG_WARN(fmt, ...)  ((void)0)
        #define SR_LOG_INFO(fmt, ...)  ((void)0)
        #define SR_LOG_DEBUG(fmt, ...) ((void)0)
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
    #define SR_THROW_IF(condition, fmt, ...) \
        do { \
            if (condition) { \
                std::string msg_ = std::format(fmt, __VA_ARGS__); \
                SR_LOG_ERROR("Runtime exception: {}", msg_); \
                throw std::runtime_error(msg_); \
            } \
        } while (0)

} // namespace SushiRuntime