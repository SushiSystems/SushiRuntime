add_executable(verify_memory_pool verify_memory_pool.cpp)

target_link_libraries(verify_memory_pool PRIVATE 
    sushiruntime
    IntelSYCL::SYCL_CXX
)

# Ensure RPATH is set so it can find libsushiruntime.so/dll
set_target_properties(verify_memory_pool PROPERTIES
    BUILD_RPATH "${CMAKE_BINARY_DIR}/src"
)
