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
 * @brief Declares TaskType, OperationID and TaskMetadata.
 * @author Mustafa Garip
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <cstddef>

#include <SushiRuntime/core/determinism.hpp>
#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/core/placement_class.hpp>

namespace SushiRuntime 
{
    namespace Internal::Graph 
    {
        /**
         * @brief Defines the high-level computational domain category for a specific task.
         *
         * Identifies the broad execution subsystem needed to handle the workload.
         */
        enum class TaskType : uint8_t 
        {
            None = 0,        /**< Empty or unitialized task. */
            MathOp,         /**< A mathematical operation (e.g. BLAS, AI Layer, Solver). */
            MemoryCopy,     /**< A data movement operation (Host2Device, Device2Device). */
            CustomHost,     /**< General C++ arbitrary host logic. */
            ControlFlow     /**< Conditional or looping node for graph control. */
        };

        /**
         * @brief Bundles fundamental metadata describing a task payload.
         *
         * Aggregates identifiers, hints and inline scalar arguments, with no heap allocation.
         */
        struct TaskMetadata 
        {
            const char* name = "unnamed_task"; /**< Profiler identifier. */
            const char* origin = nullptr;      /**< The caller's own operation the node belongs to; null for none. */
            TaskType task_type = TaskType::None;
            OperationID op_id = OperationID();

            /** @brief Group the node belongs to, for placement; a default class names none. */
            PlacementClass placement_class = PlacementClass();

            /** @brief Index within the group; none when the caller gave no key. */
            std::optional<uint64_t> placement_key = std::nullopt;

            /** @brief Determinism contract the recorder stated; a default class states none. */
            Determinism determinism = Determinism::Unstated;

            /** @brief Work one dispatch does, as the caller counted it; absent states none. */
            std::optional<NodeCost> cost = std::nullopt;

            /**
             * @brief Position of the placement rule that won for this node at compile time.
             *
             * None when no rule set was in force or no rule matched. A later dispatch reads it
             * and never asks the rules again.
             */
            std::optional<std::uint32_t> placement_rule = std::nullopt;
            
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
            T param(int index) const
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
} // namespace SushiRuntime
