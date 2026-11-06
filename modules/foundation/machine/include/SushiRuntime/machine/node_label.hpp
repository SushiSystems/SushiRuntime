/**************************************************************************/
/* node_label.hpp                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file node_label.hpp
 * @brief Declares NodeLabel, one key=value pair a machine reports about itself.
 * @author Mustafa Garip
 */

#pragma once

#include <string>

namespace SushiRuntime
{
    /**
     * @brief Holds one label of a machine.
     */
    struct NodeLabel
    {
        std::string key;   /**< Name of the label; not empty and free of '='. */
        std::string value; /**< Value of the label; may be empty. */
    };
} // namespace SushiRuntime
