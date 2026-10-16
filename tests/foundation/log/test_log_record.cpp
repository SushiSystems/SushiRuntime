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
 * @brief Tests LogRecord: SushiLog's record under its SushiRuntime name.
 * @see tests/foundation/log/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/log/log_record.hpp>

#include <type_traits>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that the SushiRuntime name is SushiLog's record type. */
TEST(Small_LogRecord, TheRecordIsSushiLogs)
{
    static_assert(std::is_same<LogRecord, SushiLog::LogRecord>::value, "one record type");
    SUCCEED();
}
