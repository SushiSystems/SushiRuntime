/**************************************************************************/
/* late_controls.hpp                                                      */
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
 * @file late_controls.hpp
 * @brief Declares LateControls, the name the surface gives the per-step controls of a node.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Names the per-step controls of a late-bound node; the surface holds them by
         *        pointer only.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        struct LateControls;
    } // namespace Detail
} // namespace SushiRuntime
