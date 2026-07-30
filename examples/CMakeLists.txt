cmake_minimum_required(VERSION 3.20)

# Each example is one self-contained binary linked against the library with the
# SYCL flags applied. The standalone tutorials always build; the distributed demo
# only when the master-worker layer is enabled (it links the distributed entry
# points). Target names follow the file names: quickstart.cpp -> sr_quickstart,
# <topic>_demo.cpp -> sr_<topic>_demo.

# On Windows the examples need sushiruntime.dll beside them to run from the build
# tree. Staged ONCE by a target every example depends on, not by a POST_BUILD step
# on each: every example writes to the same output directory, so per-target copies
# race in a parallel build — several links finish at the same moment, each tries
# to write (and vcpkg's applocal pass to read) the same sushiruntime.dll, and
# Windows answers the loser with a sharing violation. It only bites a clean build,
# because an incremental one rarely links two examples at once, which is exactly
# the kind of failure that shows up on someone else's machine.
if(WIN32)
    # Where the example binaries land. The per-target copy this replaces got it
    # from $<TARGET_FILE_DIR:...>, which is per-configuration under a multi-config
    # generator; resolved here instead, since the staging target is not tied to
    # any one example.
    if(CMAKE_CONFIGURATION_TYPES)
        set(SR_EXAMPLES_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    else()
        set(SR_EXAMPLES_STAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    add_custom_target(sushiruntime_examples_runtime_dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sushiruntime>
            "${SR_EXAMPLES_STAGE_DIR}"
        COMMENT "Staging sushiruntime.dll next to the examples")
    add_dependencies(sushiruntime_examples_runtime_dll sushiruntime)
endif()

# Builds one example target from a single source file, applying the C++17
# standard, the SYCL flags, the library link, and the Windows runtime-DLL copy.
function(sushiruntime_add_example name source)
    add_executable(${name} ${source})
    sushiruntime_apply_build_policy(${name})
    target_link_libraries(${name} PRIVATE sushiruntime)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    add_sycl_to_target(TARGET ${name} SOURCES ${source})
    if(WIN32)
        # A dependency, so the staging runs before this target links and its
        # applocal pass reads the directory.
        add_dependencies(${name} sushiruntime_examples_runtime_dll)
    endif()
endfunction()

sushiruntime_add_example(sr_quickstart             quickstart.cpp)
sushiruntime_add_example(sr_vecadd_demo            vecadd_demo.cpp)
sushiruntime_add_example(sr_stencil_demo           stencil_demo.cpp)
sushiruntime_add_example(sr_coupled_fields_demo    coupled_fields_demo.cpp)
sushiruntime_add_example(sr_game_engine_demo       game_engine_demo.cpp)
sushiruntime_add_example(sr_streaming_world_demo   streaming_world_demo.cpp)

if(SR_ENABLE_DISTRIBUTED)
    sushiruntime_add_example(sr_distributed_demo distributed_demo.cpp)
endif()
