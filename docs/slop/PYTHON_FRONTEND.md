# Python Frontend Implementation Guide

## Motivation
- **Ecosystem Integration**: Enables direct interoperability with the Python ecosystem, including NumPy, PyTorch, and Jupyter notebooks.
- **Accessibility**: Significantly lowers the barrier to entry for data scientists, ML engineers, and researchers who prefer Python over C++.
- **Rapid Prototyping**: Allows dynamic construction and execution of task graphs without the overhead of C++ compile times.

## Recommended Architecture
- **Foundation**: The existing C++ Fluent API (`api/`) remains the core foundation. The Python frontend acts as a thin wrapper around these C++ classes.
- **Binding Technology**: Use `pybind11` (or `nanobind`). `pybind11` is highly stable, well-documented, and the industry standard for C++17 projects.
- **Zero-Copy Data Transfer**: This is the most critical component for performance. The bindings must implement the Python Buffer Protocol or the DLPack standard. This ensures that when a NumPy array or PyTorch tensor is passed to the runtime, the underlying memory pointer is passed directly to the SYCL USM backend without any deep copies.

## Implementation Steps

### 1. Directory Structure
Create a dedicated `python/` directory at the project root to isolate binding code:
```text
sushiruntime/
├── CMakeLists.txt
├── python/
│   ├── CMakeLists.txt
│   └── src/
│       ├── module.cpp         (Main module definition)
│       ├── bind_runtime.cpp   (Bindings for Runtime and RuntimeContext)
│       └── bind_graph.cpp     (Bindings for Graph and Task constructs)
```

### 2. CMake Integration
Update the root `CMakeLists.txt` to introduce a build option and fetch the binding library dynamically:

```cmake
# Root CMakeLists.txt
option(SR_BUILD_PYTHON_BINDINGS "Build Python frontend via pybind11" OFF)

if(SR_BUILD_PYTHON_BINDINGS)
    include(FetchContent)
    FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG        v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
    
    add_subdirectory(python)
endif()
```

Configure the module target in `python/CMakeLists.txt`:
```cmake
# python/CMakeLists.txt
pybind11_add_module(sushiruntime_py
    src/module.cpp
    src/bind_runtime.cpp
    src/bind_graph.cpp
)

target_link_libraries(sushiruntime_py PRIVATE sushiruntime)
set_target_properties(sushiruntime_py PROPERTIES OUTPUT_NAME "sushiruntime")
```

### 3. Packaging and Distribution (`pip install`)
To enable standards-compliant installation (e.g., `pip install .`) and distribution on PyPI, use `scikit-build-core` as the PEP 517 build backend.

Define the build configuration by placing a `pyproject.toml` file at the project root:

```toml
[build-system]
requires = ["scikit-build-core", "pybind11"]
build-backend = "scikit_build_core.build"

[project]
name = "sushiruntime"
version = "0.1.0"
description = "A C++17 runtime library for task-based parallel computing on heterogeneous hardware."
readme = "README.md"
requires-python = ">=3.8"

[tool.scikit-build]
cmake.build-type = "Release"
cmake.args = ["-DSR_BUILD_PYTHON_BINDINGS=ON"]
```

#### Build Process Mechanics
When a user executes `pip install .` locally:
1. `pip` reads `pyproject.toml` and initializes an isolated build environment.
2. `scikit-build-core` is invoked as the build backend.
3. `scikit-build-core` configures the project using CMake, injecting the `-DSR_BUILD_PYTHON_BINDINGS=ON` argument.
4. The C++ project and Python extensions are compiled.
5. A Python Wheel (`.whl`) is generated and installed into the active Python environment.

For public distribution on PyPI, CI pipelines (e.g., GitHub Actions using `cibuildwheel`) should be configured to automatically build and upload pre-compiled wheels for various platforms (Linux, Windows, macOS) and Python versions.
