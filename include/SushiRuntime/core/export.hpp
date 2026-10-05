/****************************************************************/
/* export.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file export.hpp
 * @brief Defines SUSHIRUNTIME_API, the symbol export and import macro.
 * @author Mustafa Garip
 */

#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef SUSHIRUNTIME_EXPORTS
    #define SUSHIRUNTIME_API __declspec(dllexport)
  #else
    #define SUSHIRUNTIME_API __declspec(dllimport)
  #endif
#else
  #define SUSHIRUNTIME_API __attribute__((visibility("default")))
#endif