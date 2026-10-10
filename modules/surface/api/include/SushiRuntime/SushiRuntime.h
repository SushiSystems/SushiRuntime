/**************************************************************************/
/* SushiRuntime.h                                                         */
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
 * @file SushiRuntime.h
 * @brief Includes every public module of the SushiRuntime library.
 * @author Mustafa Garip
 */

#pragma once

// Core Infrastructure
#include <SushiRuntime/core/version.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/core/ref_counted.hpp>
#include <SushiRuntime/logging/logger.hpp>

// Hardware Topology & Discovery
#include <SushiRuntime/topology/hw_discovery.hpp>
#include <SushiRuntime/topology/numa_mapper.hpp>
#include <SushiRuntime/topology/affinity.hpp>

// Memory Management
#include <SushiRuntime/memory/epoch_reclamation.hpp>
#include <SushiRuntime/memory/usm_registry.hpp>

// Execution & Context
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>

// Task Graph & Scheduling
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>
#include <SushiRuntime/graph/task_graph.hpp>

// Scheduler Components
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/task/task.hpp>
#include <SushiRuntime/scheduler/range_task.hpp>
#include <SushiRuntime/scheduler/event_polling.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

// The fluent v1 API is the surface most users program against.
#include <SushiRuntime/api/fluent.hpp>
