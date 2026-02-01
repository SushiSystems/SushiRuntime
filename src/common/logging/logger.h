#pragma once

#include <format>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string_view>

#ifndef SB_ENABLE_LOGGING
    #define SB_ENABLE_LOGGING 1
#endif

namespace SushiBLAS 
{
    enum class LogType : int 
    {
        OFF   = 0,
        ERR   = 1,
        WARN  = 2,
        INFO  = 3,
        DEBUG = 4
    };

    #if SB_ENABLE_LOGGING
        #ifdef NDEBUG
            inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::INFO;
        #else
            inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::DEBUG;
        #endif
    #else
        inline constexpr LogType ACTIVE_LOG_LEVEL = LogType::OFF;
    #endif

    namespace Detail 
    {
        inline std::string get_timestamp() 
        {
            auto now = std::chrono::system_clock::now();
            return std::format("{:%H:%M:%S}", now);
        }

        template<typename... Args>
        void log_base(LogType level, std::string_view prefix, std::string_view fmt, Args&&... args) 
        {
            #if SB_ENABLE_LOGGING
                try 
                {
                    std::string msg = std::vformat(fmt, std::make_format_args(args...));
                    auto output = std::format("[{}] [{}] {}\n", get_timestamp(), prefix, msg);
                    
                    if (level <= LogType::ERR) std::cerr << output;
                    else std::cout << output;
                } 
                catch (const std::format_error& e) 
                {
                    std::cerr << "Log Format Error: " << e.what() << "\n";
                }
            #endif
        }
    }
    
    #if SB_ENABLE_LOGGING
        #define SB_LOG_ERROR(fmt, ...)                                                                  \
            if constexpr (SushiBLAS::ACTIVE_LOG_LEVEL >= SushiBLAS::LogType::ERR)                       \
                SushiBLAS::Detail::log_base(SushiBLAS::LogType::ERR, "ERROR", fmt, ##__VA_ARGS__)

        #define SB_LOG_WARN(fmt, ...)                                                                   \
            if constexpr (SushiBLAS::ACTIVE_LOG_LEVEL >= SushiBLAS::LogType::WARN)                      \
                SushiBLAS::Detail::log_base(SushiBLAS::LogType::WARN, "WARN", fmt, ##__VA_ARGS__)

        #define SB_LOG_INFO(fmt, ...)                                                                   \
            if constexpr (SushiBLAS::ACTIVE_LOG_LEVEL >= SushiBLAS::LogType::INFO)                      \
                SushiBLAS::Detail::log_base(SushiBLAS::LogType::INFO, "INFO", fmt, ##__VA_ARGS__)

        #define SB_LOG_DEBUG(fmt, ...)                                                                  \
            if constexpr (SushiBLAS::ACTIVE_LOG_LEVEL >= SushiBLAS::LogType::DEBUG)                     \
                SushiBLAS::Detail::log_base(SushiBLAS::LogType::DEBUG, "DEBUG", fmt, ##__VA_ARGS__)
    #else
        #define SB_LOG_ERROR(fmt, ...) ((void)0)
        #define SB_LOG_WARN(fmt, ...)  ((void)0)
        #define SB_LOG_INFO(fmt, ...)  ((void)0)
        #define SB_LOG_DEBUG(fmt, ...) ((void)0)
    #endif

    // SB_ASSERT: Only works in Debug mode. For critical internal errors.
    #ifndef NDEBUG
        #define SB_ASSERT(condition, fmt, ...)                                                      \
            do {                                                                                    \
                if (!(condition)) {                                                                 \
                    SB_LOG_ERROR("ASSERTION FAILED: " fmt " | {}:{}", ##__VA_ARGS__,                \
                                 __FILE__, __LINE__);                                               \
                    std::abort();                                                                   \
                }                                                                                   \
            } while (0)
    #else
        #define SB_ASSERT(condition, fmt, ...) ((void)0)
    #endif

    // SB_THROW_IF: Always works. Throws runtime_error for user errors.
    #define SB_THROW_IF(condition, fmt, ...)                                                       \
        do {                                                                                        \
            if (condition) {                                                                        \
                std::string msg_ = std::format(fmt, ##__VA_ARGS__);                                 \
                SB_LOG_ERROR("Runtime Error: {}", msg_);                                            \
                throw std::runtime_error(msg_);                                                     \
            }                                                                                       \
        } while (0)

    // SB_CUDA_CHECK: Catches CUDA API errors.
    #ifdef USE_CUDA
        #define SB_CUDA_CHECK(call)                                                                 \
            do {                                                                                    \
                cudaError_t err = call;                                                             \
                if (err != cudaSuccess) {                                                           \
                    SB_LOG_ERROR("CUDA Error at {}:{} -> {}",                                       \
                                 __FILE__, __LINE__, cudaGetErrorString(err));                      \
                    throw std::runtime_error("CUDA Failure");                                       \
                }                                                                                   \
            } while (0)
    #endif

} // namespace SushiBLAS