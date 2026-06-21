/**************************************************************************/
/* task_types.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
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
         * @brief Defines the high-level computational domain category for a specific task.
         *
         * Identifies the broad execution subsystem needed to handle the workload.
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
         * @brief Uniquely identifies a discrete computational operation using a strongly typed hash wrapper.
         *
         * Generates deterministic 32-bit identifiers via FNV-1a hashing to provide an open, string-based registration system without rigid enums.
         */
        struct OpID 
        {
            uint32_t value;

            /**
             * @brief Default constructs an empty, zero-valued operation identifier.
             *
             * Creates a placeholder instance representing an uninitialized or null operation.
             */
            constexpr OpID() : value(0) {}

            /**
             * @brief Constructs an operation identifier directly from a raw hash value.
             *
             * Encapsulates an explicit integer to form the strongly typed identity.
             *
             * @param val The raw 32-bit integer representing the operation hash.
             */
            constexpr explicit OpID(uint32_t val) : value(val) {}

            /**
             * @brief Computes a compile-time 32-bit FNV-1a hash for string inputs.
             *
             * Performs a purely deterministic character-by-character transformation to derive a unique numeric identifier.
             *
             * @param str A pointer to the character array to process.
             * @param length The total number of characters within the target string.
             * @return The resulting 32-bit numerical hash.
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

            constexpr bool operator==(const OpID& other) const  { return value == other.value; }
            constexpr bool operator!=(const OpID& other) const  { return value != other.value; }
            constexpr bool operator<(const OpID& other) const   { return value < other.value; }
        };

        namespace Literals 
        {
            /**
             * @brief Generates a typed operation identifier using a user-defined string literal.
             *
             * Invokes the compile-time FNV-1a hashing algorithm to convert a readable string directly into an OpID.
             *
             * @param str A pointer to the character sequence defining the operation name.
             * @param len The length of the provided character sequence.
             * @return The strongly typed, compile-time evaluated operation identifier.
             */
            constexpr OpID operator""_op(const char* str, std::size_t len) 
            {
                return OpID(OpID::hash_str(str, len));
            }
        }

        /**
         * @brief Bundles fundamental metadata describing a task payload.
         *
         * Aggregates identifiers, hints, and inline scalar arguments into a completely allocation-free layout.
         */
        struct TaskMetadata 
        {
            const char* name = "unnamed_task"; /**< Profiler identifier. */
            TaskType task_type = TaskType::NONE;
            OpID op_id = OpID(0);
            
            /**
             * @brief Influences task execution order during queue retrieval.
             *
             * Larger numeric values instruct the scheduler to prioritize this task over others.
             */
            int priority = 0;
            
            /**
             * @brief Suggests a preferred hardware NUMA node for optimal execution locality.
             *
             * A negative integer indicates the task may be scheduled universally without affinity constraints.
             */
            int numa_affinity = -1;
            
            /**
             * @brief Defines the maximum number of custom 64-bit scalar parameters held by a task.
             *
             * Bounds the static capacity of the internal inline storage array.
             */
            static constexpr size_t K_MAX_PARAMS = 8;

            /**
             * @brief Holds up to 64 bytes of inline parameter data.
             *
             * Stores contiguous numeric values locally to prevent dynamic allocations during dispatch.
             */
            uint64_t params[K_MAX_PARAMS] = {0};

            /**
             * @brief Injects a primitive scalar value into the inline metadata buffer.
             *
             * Converts and copies trivially-copyable types into the specific designated internal 64-bit slot.
             *
             * @param index The zero-based array index identifying the storage slot.
             * @param value The primitive data payload to save.
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
             * @brief Extracts a primitive scalar value from the internal metadata buffer.
             *
             * Reinterprets the underlying raw bytes of a designated storage slot back into the specified type.
             *
             * @param index The zero-based array index identifying the storage slot.
             * @return The correctly typed reconstructed primitive value.
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
