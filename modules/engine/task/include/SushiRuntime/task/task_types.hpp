/**************************************************************************/
/* task_types.hpp                                                         */
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
 * @file task_types.hpp
 * @brief Declares TaskType, OpID and TaskMetadata.
 * @author Mustafa Garip
 */

#pragma once

#include <cassert>
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
         * @brief Identifies a computational operation by the 32-bit FNV-1a hash of its name.
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
                    // char is signed on some platforms; each byte is read as unsigned.
                    hash ^= static_cast<uint32_t>(static_cast<unsigned char>(str[i]));
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
         * Aggregates identifiers, hints and inline scalar arguments, with no heap allocation.
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
             * @param index The zero-based array index identifying the storage slot.
             * @param value The primitive data payload to save.
             * @pre 0 <= index < K_MAX_PARAMS; a release build ignores an index outside it.
             */
            template<typename T>
            void set_param(int index, T value)
            {
                static_assert(std::is_trivially_copyable_v<T>, "Parameter must be trivially copyable.");
                static_assert(sizeof(T) <= sizeof(uint64_t), "Parameter must fit in 64 bits.");
                assert(index >= 0 && index < static_cast<int>(K_MAX_PARAMS));
                if (index >= 0 && index < static_cast<int>(K_MAX_PARAMS))
                {
                    std::memcpy(&params[index], &value, sizeof(T));
                }
            }

            /**
             * @brief Extracts a primitive scalar value from the internal metadata buffer.
             *
             * @param index The zero-based array index identifying the storage slot.
             * @return The value read from the slot as T.
             * @pre 0 <= index < K_MAX_PARAMS; a release build returns T{} for an index outside it.
             */
            template<typename T>
            T get_param(int index) const
            {
                static_assert(std::is_trivially_copyable_v<T>, "Parameter must be trivially copyable.");
                static_assert(sizeof(T) <= sizeof(uint64_t), "Parameter must fit in 64 bits.");
                assert(index >= 0 && index < static_cast<int>(K_MAX_PARAMS));
                T value{};
                if (index >= 0 && index < static_cast<int>(K_MAX_PARAMS))
                {
                    std::memcpy(&value, &params[index], sizeof(T));
                }
                return value;
            }
        };

    } // namespace Graph

    using Graph::Literals::operator""_op;
    using Graph::OpID;
    using Graph::TaskMetadata;

} // namespace SushiRuntime
