/**************************************************************************/
/* SushiRuntime.h                                                         */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/**************************************************************************/

#pragma once

/**
 * @file SushiRuntime.h
 * @brief Master header for the SushiRuntime library.
 * 
 * Including this file provides access to all public modules of the runtime.
 */

// Core Infrastructure
#include "core/export.hpp"
#include "core/common.hpp"
#include "core/sushi_ptr.hpp"
#include "core/ref_counted.hpp"
#include "core/logger.hpp"

// Hardware Topology & Discovery
#include "topology/hw_discovery.hpp"
#include "topology/numa_mapper.hpp"

// Memory Management
#include "memory/usm_allocator.hpp"
#include "memory/memory_pool.hpp"

// Execution & Context
#include "execution/runtime_context.hpp"

// Task Graph & Scheduling
#include "graph/node.hpp"
#include "graph/task_graph.hpp"
#include "graph/dependency_tracker.hpp"

// Scheduler Components
#include "scheduler/task_scheduler.hpp"
#include "scheduler/task.hpp"
#include "scheduler/event_polling.hpp"
