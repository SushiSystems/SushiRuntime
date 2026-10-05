/****************************************************************/
/* export.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

/**
 * @file export.hpp
 * @brief Defines macro utilities for library symbol export and import.
 * 
 * Configures platform-specific attributes to control the visibility of public API symbols.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef SUSHIRUNTIME_EXPORTS
    #define SUSHIRUNTIME_API __declspec(dllexport)
  #else
    #define SUSHIRUNTIME_API __declspec(dllimport)
  #endif
#else
  #define SUSHIRUNTIME_API __attribute__((visibility("default")))
#endif