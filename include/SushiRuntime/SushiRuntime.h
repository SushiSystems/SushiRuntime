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
#include "version.hpp"
#include "core/export.hpp"
#include "core/common.hpp"
#include "core/error.hpp"
#include "core/sushi_ptr.hpp"
#include "core/ref_counted.hpp"
#include "core/logger.hpp"

// Hardware Topology & Discovery
#include "topology/hw_discovery.hpp"
#include "topology/numa_mapper.hpp"
#include "topology/affinity.hpp"

// Memory Management
#include "memory/epoch_reclamation.hpp"
#include "memory/usm_registry.hpp"

// Execution & Context
#include "execution/execution_engine.hpp"
#include "execution/runtime_context.hpp"

// Task Graph & Scheduling
#include "graph/node.hpp"
#include "graph/task_graph.hpp"
#include "graph/dependency_tracker.hpp"

// Scheduler Components
#include "scheduler/task_scheduler.hpp"
#include "scheduler/task.hpp"
#include "scheduler/event_polling.hpp"
#include "scheduler/rebalancer_interfaces.hpp"

// The fluent v1 API is the surface most users program against.
#include "api/fluent.hpp"
