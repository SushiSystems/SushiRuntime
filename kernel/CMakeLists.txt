# kernel/CMakeLists.txt

set(SB_KERNEL_FILES
    "cpu/ref/ref_sgemm.cpp"
    "cpu/ref/sgemm_4x4.cpp"
)

# Future: Add AVX/SIMD specific kernels here
# if(MSVC AND AVX2_FOUND) ...

add_library(SushiBLAS_kernels STATIC ${SB_KERNEL_FILES})

target_include_directories(SushiBLAS_kernels PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(SushiBLAS_kernels PRIVATE /arch:AVX2 /O2)
endif()
