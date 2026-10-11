/**************************************************************************/
/* distributed.hpp                                                        */
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
 * @file distributed.hpp
 * @brief Includes the distributed headers a master or a worker program needs.
 * @author Mustafa Garip
 * @see modules/extension/distributed/README.md
 */

#pragma once

#include <SushiRuntime/distributed/buffer_handle.hpp>
#include <SushiRuntime/distributed/buffer_view.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/cluster_statistics.hpp>
#include <SushiRuntime/distributed/endpoint.hpp>
#include <SushiRuntime/distributed/offload_policy.hpp>
#include <SushiRuntime/distributed/transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/registered_task.hpp>
#include <SushiRuntime/distributed/shared_secret.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/worker_main.hpp>
