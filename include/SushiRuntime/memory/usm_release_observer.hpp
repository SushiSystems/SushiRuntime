/**************************************************************************/
/* usm_release_observer.hpp                                               */
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
 * @file usm_release_observer.hpp
 * @brief Declares UsmReleaseObserver, which UsmAllocator tells about every release.
 * @author Mustafa Garip
 * @see include/SushiRuntime/memory/README.md
 */

#pragma once

namespace SushiRuntime
{
    namespace Memory
    {
        /**
         * @brief Receives the address of every allocation UsmAllocator::release() is given.
         * @see include/SushiRuntime/memory/README.md
         */
        class UsmReleaseObserver
        {
            public:
                /**
                 * @brief Hears that an address is about to be released.
                 * @param address Never null. The memory is still allocated during the call and
                 *        may be freed as soon as it returns. Any thread may call, several at once.
                 */
                virtual void on_release(void* address) noexcept = 0;

            protected:
                /** @brief Keeps an observer from being destroyed through this interface. */
                ~UsmReleaseObserver() = default;
        };

    } // namespace Memory
} // namespace SushiRuntime
