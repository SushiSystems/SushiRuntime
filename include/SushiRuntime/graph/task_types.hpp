/****************************************************************/
/* task_types.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file task_types.hpp
 * @brief Declares TaskType, OpID and TaskMetadata.
 * @author Mustafa Garip
 */

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

            /** @brief Computes a compile-time 32-bit FNV-1a hash of a string. */
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
             * @brief Builds a typed operation identifier from a string literal with the FNV-1a
             *        hash.
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
