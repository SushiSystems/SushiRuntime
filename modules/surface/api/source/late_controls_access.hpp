/**************************************************************************/
/* late_controls_access.hpp                                               */
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
 * @file late_controls_access.hpp
 * @brief Defines the conversion between Detail::LateControls and the engine's late bindings.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <memory>

#include <SushiRuntime/api/detail/late_controls.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Returns the late bindings surface controls name, read-only; null for null. */
        [[nodiscard]] inline const Internal::Graph::LateBindings*
        engine_controls(const LateControls* controls) noexcept
        {
            return reinterpret_cast<const Internal::Graph::LateBindings*>(controls);
        }

        /** @brief Returns a share of the late bindings surface controls name; null for null. */
        [[nodiscard]] inline std::shared_ptr<Internal::Graph::LateBindings>
        engine_controls(const std::shared_ptr<LateControls>& controls) noexcept
        {
            return std::shared_ptr<Internal::Graph::LateBindings>(
                controls, reinterpret_cast<Internal::Graph::LateBindings*>(controls.get()));
        }

        /** @brief Returns a share of late bindings under the surface's name; null for null. */
        [[nodiscard]] inline std::shared_ptr<LateControls>
        surface_controls(const std::shared_ptr<Internal::Graph::LateBindings>& bindings) noexcept
        {
            return std::shared_ptr<LateControls>(
                bindings, reinterpret_cast<LateControls*>(bindings.get()));
        }
    } // namespace Detail
} // namespace SushiRuntime
