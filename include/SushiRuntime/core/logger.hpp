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

#include <mutex>
#include <chrono>
#include <format>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <source_location>

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
            /** @brief Mutex for thread-safe output. */
            inline std::mutex log_mutex;

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
                        
                        std::lock_guard<std::mutex> lock(log_mutex);
                        if (level <= LogType::ERR) std::cerr << output;
                        else std::cout << output;
                    } 
                    catch (const std::format_error& e) 
                    {
                        std::cerr << "Log format error: " << e.what() << "\n";
                    }
                #endif
            }
        }
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