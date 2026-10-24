# SushiRuntimeTiers.cmake
# SushiRuntime - https://github.com/SushiSystems/SushiRuntime
# Copyright (c) 2026-present Mustafa Garip & Sushi Systems
# Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.
# Commercial use requires a licence from Sushi Systems.

# The module order of the repository, stated once. sushiruntime_add_module() refuses a
# dependency this file does not allow, and modules/CMakeLists.txt adds the folders from it.

# Lowest tier first. A module may depend on a module of a lower tier.
set(SUSHIRUNTIME_TIER_ORDER
    foundation
    platform
    engine
    surface
    extension)

# Module, then its tier. Inside a tier a dependency is listed before its consumer.
set(SUSHIRUNTIME_MODULE_TIERS
    core            foundation
    report          foundation
    region          foundation
    log             foundation
    machine         foundation
    placement       foundation
    sycl            foundation
    test_runner     foundation
    measurement     foundation
    thread_barrier  foundation
    device_probe    foundation
    usm_guard       foundation
    numa_lookup     platform
    topology        platform
    memory          platform
    device          platform
    observability   platform
    task            engine
    dependency_tracking engine
    work_stealing   engine
    scheduler       engine
    graph           engine
    execution       engine
    region_boundary engine
    probe_tasks     engine
    runtime_assembly engine
    engine_handles  surface
    interop         surface
    memory_exchange  surface
    context         surface
    context_access  surface
    api             surface
    transport       extension
    wire            extension
    link_security   extension
    network_socket  extension
    frame_transport extension
    buffer_exchange extension
    pending_tasks   extension
    dispatch_deduplication extension
    process_environment extension
    cluster_load    extension
    offload_coordination extension
    distributed     extension)

# Consumer, then dependency: the edges allowed between two modules of one tier.
set(SUSHIRUNTIME_SAME_TIER_EDGES
    report          core
    sycl            core
    placement       core
    placement       machine
    test_runner     log
    dependency_tracking task
    work_stealing   task
    topology        numa_lookup
    scheduler       task
    scheduler       work_stealing
    graph           dependency_tracking
    graph           task
    execution       task
    execution       scheduler
    execution       graph
    region_boundary task
    probe_tasks     task
    runtime_assembly execution
    runtime_assembly scheduler
    memory_exchange  interop
    context_access  context
    api             engine_handles
    api             interop
    api             memory_exchange
    api             context
    api             context_access
    wire            transport
    network_socket  transport
    frame_transport transport
    frame_transport wire
    buffer_exchange transport
    buffer_exchange wire
    pending_tasks   transport
    offload_coordination transport
    offload_coordination wire
    offload_coordination link_security
    offload_coordination cluster_load
    offload_coordination buffer_exchange
    offload_coordination pending_tasks
    distributed     cluster_load
    distributed     offload_coordination
    distributed     buffer_exchange
    distributed     pending_tasks
    distributed     dispatch_deduplication
    distributed     process_environment
    distributed     transport
    distributed     wire
    distributed     link_security
    distributed     network_socket
    distributed     frame_transport)

# Module, then the option that switches it on. A module not listed is always built.
set(SUSHIRUNTIME_OPTIONAL_MODULES
    test_runner     SR_BUILD_TESTS
    measurement     SR_BUILD_TESTS
    thread_barrier  SR_BUILD_TESTS
    device_probe    SR_BUILD_TESTS
    usm_guard       SR_BUILD_TESTS
    probe_tasks     SR_BUILD_TESTS
    runtime_assembly SR_BUILD_TESTS
    transport       SR_ENABLE_DISTRIBUTED
    wire            SR_ENABLE_DISTRIBUTED
    link_security   SR_ENABLE_DISTRIBUTED
    network_socket  SR_ENABLE_DISTRIBUTED
    frame_transport SR_ENABLE_DISTRIBUTED
    buffer_exchange SR_ENABLE_DISTRIBUTED
    pending_tasks   SR_ENABLE_DISTRIBUTED
    dispatch_deduplication SR_ENABLE_DISTRIBUTED
    process_environment SR_ENABLE_DISTRIBUTED
    cluster_load    SR_ENABLE_DISTRIBUTED
    offload_coordination SR_ENABLE_DISTRIBUTED
    distributed     SR_ENABLE_DISTRIBUTED)

# Sets <out> to the position of <tier> in SUSHIRUNTIME_TIER_ORDER, or -1.
function(sushiruntime_tier_index tier out)
    list(FIND SUSHIRUNTIME_TIER_ORDER "${tier}" index)
    set(${out} ${index} PARENT_SCOPE)
endfunction()

# Sets <out> to the tier of <module>, or to an empty string for a module the table lacks.
function(sushiruntime_module_tier module out)
    set(result "")
    list(LENGTH SUSHIRUNTIME_MODULE_TIERS length)
    math(EXPR last "${length} - 2")
    foreach(position RANGE 0 ${last} 2)
        list(GET SUSHIRUNTIME_MODULE_TIERS ${position} name)
        if(name STREQUAL "${module}")
            math(EXPR tier_position "${position} + 1")
            list(GET SUSHIRUNTIME_MODULE_TIERS ${tier_position} result)
            break()
        endif()
    endforeach()
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

# Sets <out> to TRUE when SUSHIRUNTIME_SAME_TIER_EDGES lists <consumer> -> <dependency>.
function(sushiruntime_same_tier_edge_allowed consumer dependency out)
    set(result FALSE)
    list(LENGTH SUSHIRUNTIME_SAME_TIER_EDGES length)
    math(EXPR last "${length} - 2")
    foreach(position RANGE 0 ${last} 2)
        math(EXPR dependency_position "${position} + 1")
        list(GET SUSHIRUNTIME_SAME_TIER_EDGES ${position} edge_consumer)
        list(GET SUSHIRUNTIME_SAME_TIER_EDGES ${dependency_position} edge_dependency)
        if(edge_consumer STREQUAL "${consumer}" AND edge_dependency STREQUAL "${dependency}")
            set(result TRUE)
            break()
        endif()
    endforeach()
    set(${out} ${result} PARENT_SCOPE)
endfunction()

# Sets <out> to TRUE when <module> is built: it has no option, or its option is on.
function(sushiruntime_module_enabled module out)
    set(result TRUE)
    list(LENGTH SUSHIRUNTIME_OPTIONAL_MODULES length)
    math(EXPR last "${length} - 2")
    foreach(position RANGE 0 ${last} 2)
        math(EXPR option_position "${position} + 1")
        list(GET SUSHIRUNTIME_OPTIONAL_MODULES ${position} name)
        list(GET SUSHIRUNTIME_OPTIONAL_MODULES ${option_position} option_name)
        if(name STREQUAL "${module}" AND NOT ${option_name})
            set(result FALSE)
        endif()
    endforeach()
    set(${out} ${result} PARENT_SCOPE)
endfunction()

# Sets <out> to "<tier>/<module>" for every enabled module, in table order.
function(sushiruntime_enabled_module_folders out)
    set(folders "")
    list(LENGTH SUSHIRUNTIME_MODULE_TIERS length)
    math(EXPR last "${length} - 2")
    foreach(position RANGE 0 ${last} 2)
        math(EXPR tier_position "${position} + 1")
        list(GET SUSHIRUNTIME_MODULE_TIERS ${position} name)
        list(GET SUSHIRUNTIME_MODULE_TIERS ${tier_position} tier)
        sushiruntime_module_enabled("${name}" enabled)
        if(enabled)
            list(APPEND folders "${tier}/${name}")
        endif()
    endforeach()
    set(${out} "${folders}" PARENT_SCOPE)
endfunction()
