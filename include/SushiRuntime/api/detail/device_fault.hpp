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
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /** @brief Words one device fault record holds: kind, base, count, capacity. */
            constexpr std::size_t DEVICE_FAULT_WORDS = 4;

            /** @brief Fault kind of a record no kernel has written. */
            constexpr std::uint32_t DEVICE_FAULT_NONE = 0;

            /** @brief Fault kind of a late-bound window that does not fit its capacity. */
            constexpr std::uint32_t DEVICE_FAULT_WINDOW = 1;

            /** @brief Fault kind of a LateCount whose device value exceeds its capacity. */
            constexpr std::uint32_t DEVICE_FAULT_LATE_COUNT = 2;

            /**
             * @brief Writes one fault record from device code; its node is the record's only writer.
             * @param record   DEVICE_FAULT_WORDS host-USM words; null records nothing.
             * @param kind     DEVICE_FAULT_WINDOW or DEVICE_FAULT_LATE_COUNT.
             */
            inline void record_device_fault(std::uint32_t* record, std::uint32_t kind,
                                            std::size_t base, std::size_t count,
                                            std::size_t capacity) noexcept
            {
                if (record == nullptr)
                    return;
                record[1] = static_cast<std::uint32_t>(base);
                record[2] = static_cast<std::uint32_t>(count);
                record[3] = static_cast<std::uint32_t>(capacity);
                record[0] = kind;
            }

            /**
             * @brief Clears a fault record the device wrote and throws the error it names.
             * @param record DEVICE_FAULT_WORDS host-USM words, read after its step finished.
             * @throws Core::SushiException With invalid_graph when the record holds a fault.
             */
            inline void throw_if_device_fault(std::uint32_t* record)
            {
                const std::uint32_t kind = record[0];
                if (kind == DEVICE_FAULT_NONE)
                    return;
                const std::string base = std::to_string(record[1]);
                const std::string count = std::to_string(record[2]);
                const std::string capacity = std::to_string(record[3]);
                record[0] = DEVICE_FAULT_NONE;
                if (kind == DEVICE_FAULT_LATE_COUNT)
                    throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                        "LateCount: the live count (" + count +
                        ") exceeds the capacity it was planned for (" + capacity + ")."});
                throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                    "A late-bound task's live window starts at " + base + " and covers " +
                    count + " elements, which does not fit the capacity it was compiled for (" +
                    capacity + "); size the task's buffers for the worst case."});
            }

        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
