/**************************************************************************/
/* version.cpp                                                            */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <SushiRuntime/version.hpp>

namespace SushiRuntime
{
    // These read the same macros the consumer sees, but they are frozen here at
    // the moment the LIBRARY was compiled. That is the whole point: a consumer
    // comparing its own macros against these is comparing two builds, and they
    // only agree if the binary it loaded is the one its headers describe.

    Version runtime_version() noexcept
    {
        return Version{SUSHIRUNTIME_VERSION_MAJOR,
                       SUSHIRUNTIME_VERSION_MINOR,
                       SUSHIRUNTIME_VERSION_PATCH};
    }

    const char* runtime_version_string() noexcept
    {
        return SUSHIRUNTIME_VERSION_STRING;
    }

} // namespace SushiRuntime
