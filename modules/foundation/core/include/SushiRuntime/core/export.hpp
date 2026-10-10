/**************************************************************************/
/* export.hpp                                                             */
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
 * @file export.hpp
 * @brief Defines SUSHIRUNTIME_API, SR_API and SR_TYPE_API, the symbol visibility macros.
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

/** @brief Short spelling of SUSHIRUNTIME_API. */
#define SR_API SUSHIRUNTIME_API

/**
 * @brief Marks a type defined whole in a header whose type information crosses the library.
 *
 * The library is compiled with hidden visibility, so an exception thrown inside it and
 * caught by a consumer needs type information both sides can see. The macro is empty on
 * Windows, where an inline class is neither exported nor imported.
 */
#if defined(_WIN32) || defined(__CYGWIN__)
  #define SR_TYPE_API
#else
  #define SR_TYPE_API __attribute__((visibility("default")))
#endif