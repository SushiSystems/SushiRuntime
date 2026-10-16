/**************************************************************************/
/* test_log_record.cpp                                                    */
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
 * @file test_log_record.cpp
 * @brief Tests LogRecord: the fields a sink reads, in the order the C record keeps.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_record.hpp>

#include <type_traits>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that a record is a plain value whose fields are given in the documented order. */
TEST(Small_LogRecord, TheFieldsFollowTheDocumentedOrder)
{
    static_assert(std::is_trivially_copyable<LogRecord>::value, "a record is copied as a value");
    constexpr LogRecord record{LogLevel::Error, "project", "module", "file.cpp", 9, 2, 7,
                               "message", "text"};
    static_assert(record.level == LogLevel::Error, "level first");
    static_assert(record.project == "project" && record.module == "module", "then the category");
    static_assert(record.file == "file.cpp" && record.line == 9, "then the call site");
    static_assert(record.thread == 2 && record.time_ns == 7, "then the thread and the time");
    static_assert(record.message == "message" && record.text == "text", "then the two texts");
    SUCCEED();
}
