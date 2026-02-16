/**************************************************************************/
/* export.hpp                                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef SUSHIRUNTIME_EXPORTS
    #define SUSHIRUNTIME_API __declspec(dllexport)
  #else
    #define SUSHIRUNTIME_API __declspec(dllimport)
  #endif
#else
  #define SUSHIRUNTIME_API __attribute__((visibility("default")))
#endif