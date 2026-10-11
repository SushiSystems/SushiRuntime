/**************************************************************************/
/* device_fault.hpp                                                       */
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
 * @file device_fault.hpp
 * @brief Declares the device fault record and the functions that write and check it.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Words one device fault record holds: kind, base, count, capacity. */
        constexpr std::size_t K_DEVICE_FAULT_WORDS = 4;

        /** @brief Fault kind of a record no kernel has written. */
        constexpr std::uint32_t K_DEVICE_FAULT_NONE = 0;

        /** @brief Fault kind of a late-bound window that does not fit its capacity. */
        constexpr std::uint32_t K_DEVICE_FAULT_WINDOW = 1;

        /** @brief Fault kind of a LateCount whose device value exceeds its capacity. */
        constexpr std::uint32_t K_DEVICE_FAULT_LATE_COUNT = 2;

        /** @brief Returns a size as a fault word, or UINT32_MAX when it does not fit one. */
        constexpr std::uint32_t saturated_fault_word(std::size_t value) noexcept
        {
            return value > UINT32_MAX ? UINT32_MAX : static_cast<std::uint32_t>(value);
        }

        /**
         * @brief Writes one fault record, from a kernel or from the host code that dispatches
         *        the node; its node is the record's only writer.
         * @param record   K_DEVICE_FAULT_WORDS words the host can read; null records nothing.
         * @param kind     K_DEVICE_FAULT_WINDOW or K_DEVICE_FAULT_LATE_COUNT.
         * @param base     Stored as UINT32_MAX when larger, as count and capacity are.
         */
        inline void record_device_fault(std::uint32_t* record, std::uint32_t kind,
                                        std::size_t base, std::size_t count,
                                        std::size_t capacity) noexcept
        {
            if (record == nullptr)
                return;
            record[1] = saturated_fault_word(base);
            record[2] = saturated_fault_word(count);
            record[3] = saturated_fault_word(capacity);
            record[0] = kind;
        }

        /**
         * @brief Builds the error of a LateCount whose live count exceeds its capacity.
         * @return An InvalidGraph error naming both numbers, for a host or a device count.
         */
        inline Error late_count_overflow(std::size_t live, std::size_t capacity)
        {
            return Error{ErrorCode::InvalidGraph,
                "LateCount: the live count (" + std::to_string(live) +
                ") exceeds the capacity it was planned for (" + std::to_string(capacity) +
                "); build the LateCount with a capacity that covers the largest count."};
        }

        /**
         * @brief Clears a fault record and returns the error it names.
         * @param record K_DEVICE_FAULT_WORDS words, read after the step that wrote them finished.
         * @return An InvalidGraph error when the record holds a fault, or nothing.
         */
        [[nodiscard]] inline std::optional<Error> take_device_fault(std::uint32_t* record)
        {
            const std::uint32_t kind = record[0];
            if (kind == K_DEVICE_FAULT_NONE)
                return std::nullopt;
            const std::uint32_t base = record[1];
            const std::uint32_t count = record[2];
            const std::uint32_t capacity = record[3];
            record[0] = K_DEVICE_FAULT_NONE;
            if (kind == K_DEVICE_FAULT_LATE_COUNT)
                return late_count_overflow(count, capacity);
            return Error{ErrorCode::InvalidGraph,
                "A late-bound task's live window starts at " + std::to_string(base) +
                " and covers " + std::to_string(count) +
                " elements, which does not fit the capacity it was compiled for (" +
                std::to_string(capacity) + "); size the task's buffers for the worst case."};
        }

    } // namespace Detail
} // namespace SushiRuntime
