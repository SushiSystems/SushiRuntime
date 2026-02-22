/**************************************************************************/
/* task_types.hpp                                                         */
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

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <cstddef>

namespace SushiRuntime 
{
    namespace Graph 
    {
        /**
         * @brief Super-category of the task describing the high-level computational domain.
         */
        enum class TaskType : uint8_t 
        {
            NONE = 0,        /**< Empty or unitialized task. */
            MATH_OP,         /**< A mathematical operation (e.g. BLAS, AI Layer, Solver). */
            MEMORY_COPY,     /**< A data movement operation (Host2Device, Device2Device). */
            CUSTOM_HOST,     /**< General C++ arbitrary host logic. */
            CONTROL_FLOW     /**< Conditional or looping node for graph control. */
        };

        /**
         * @brief Strongly typed modular Open Operation ID system based on FNV-1a string hashing.
         * Replaces rigid enums allowing any compute domain to register literal operations.
         */
        struct OpID 
        {
            uint32_t value;

            constexpr OpID() : value(0) {}
            constexpr explicit OpID(uint32_t val) : value(val) {}

            /**
             * @brief Compile-time 32-bit FNV-1a hash algorithm.
             */
            static constexpr uint32_t hash_str(const char* str, std::size_t length) 
            {
                uint32_t hash = 2166136261u;
                for (std::size_t i = 0; i < length; ++i) 
                {
                    hash ^= static_cast<uint32_t>(str[i]);
                    hash *= 16777619u;
                }
                return hash;
            }

            constexpr bool operator==(const OpID& other) const { return value == other.value; }
            constexpr bool operator!=(const OpID& other) const { return value != other.value; }
            constexpr bool operator<(const OpID& other) const { return value < other.value; }
        };

        namespace Literals 
        {
            /**
             * @brief User Defined Literal creating a compile-time OpID.
             * Example: "blas.gemm"_op creates an invariant uint32_t hash ID.
             */
            constexpr OpID operator""_op(const char* str, std::size_t len) 
            {
                return OpID(OpID::hash_str(str, len));
            }
        }

        /**
         * @brief Lightweight strongly typed container for operations metadata.
         * Allocations-free (no strings or maps under the hood).
         */
        struct TaskMetadata 
        {
            const char* name = "unnamed_task"; /**< Profiler identifier. */
            TaskType task_type = TaskType::NONE;
            OpID op_id = OpID(0);
            
            /** Storage for scalar parameters up to 64 bytes total (e.g., alphas, betas). */
            static constexpr size_t K_MAX_PARAMS = 8;
            uint64_t params[K_MAX_PARAMS] = {0};

            /**
             * @brief Saves a generic scalar value into the parameters buffer.
             */
            template<typename T>
            void set_param(int index, T value) 
            {
                static_assert(std::is_trivially_copyable_v<T>, "Parameter must be trivially copyable.");
                static_assert(sizeof(T) <= sizeof(uint64_t), "Parameter must fit in 64 bits.");
                if (index >= 0 && index < static_cast<int>(K_MAX_PARAMS)) 
                {
                    std::memcpy(&params[index], &value, sizeof(T));
                }
            }

            /**
             * @brief Restores a scalar value from the parameters buffer.
             */
            template<typename T>
            T get_param(int index) const 
            {
                static_assert(std::is_trivially_copyable_v<T>, "Parameter must be trivially copyable.");
                static_assert(sizeof(T) <= sizeof(uint64_t), "Parameter must fit in 64 bits.");
                T value{};
                if (index >= 0 && index < static_cast<int>(K_MAX_PARAMS)) 
                {
                    std::memcpy(&value, &params[index], sizeof(T));
                }
                return value;
            }
        };

    } // namespace Graph
} // namespace SushiRuntime
