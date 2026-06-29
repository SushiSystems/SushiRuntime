cmake_minimum_required(VERSION 3.20)

# Each example is one self-contained binary linked against the library with the
# SYCL flags applied. The standalone tutorials always build; the distributed demo
# only when the master-worker layer is enabled (it links the distributed entry
# points). Target names follow the file names: quickstart.cpp -> sr_quickstart,
# <topic>_demo.cpp -> sr_<topic>_demo.

# Builds one example target from a single source file, applying the C++17
# standard, the SYCL flags, the library link, and the Windows runtime-DLL copy.
function(sushi_add_example name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE sushiruntime)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)
    add_sycl_to_target(TARGET ${name} SOURCES ${source})
    if(WIN32)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:sushiruntime>
                $<TARGET_FILE_DIR:${name}>)
    endif()
endfunction()

sushi_add_example(sr_quickstart             quickstart.cpp)
sushi_add_example(sr_vecadd_demo            vecadd_demo.cpp)
sushi_add_example(sr_stencil_demo           stencil_demo.cpp)
sushi_add_example(sr_coupled_fields_demo    coupled_fields_demo.cpp)
sushi_add_example(sr_game_engine_demo       game_engine_demo.cpp)
sushi_add_example(sr_streaming_world_demo   streaming_world_demo.cpp)

if(SR_ENABLE_DISTRIBUTED)
    sushi_add_example(sr_distributed_demo distributed_demo.cpp)
endif()
