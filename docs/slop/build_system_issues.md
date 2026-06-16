# Build System and Architectural Issues

During the review of the project's build system (`CMakeLists.txt`) and architecture, several disadvantageous, restrictive, and obsolete patterns were identified:

### 1. Hardcoded Paths (CMake Anti-Pattern)
The build system currently relies on hardcoded absolute paths, which breaks portability across different machines, CI/CD pipelines, and operating systems.
* **Windows:** Paths like `C:/vcpkg` and `C:/Program Files (x86)/Intel/oneAPI...` are explicitly hardcoded. If a developer installs `vcpkg` or the compiler in a different drive or directory, the build will fail.
* **Linux:** The compiler flags include highly specific paths such as `-I/usr/include/c++/13` and `--gcc-toolchain=/usr`. This assumes an environment similar to Ubuntu 22.04 with GCC 13 installed. It will likely break on other Linux distributions like Fedora or Arch Linux.
* **Recommendation:** Use CMake's standard package discovery features (`find_path`, `find_package`) and rely on environment variables (like `VCPKG_ROOT`) instead of hardcoding absolute paths.

### 2. Obsolete CUDA Architecture Target (`sm_61`)
When the CUDA backend is enabled (`SUSHI_ENABLE_CUDA=ON`), the compiler is strictly forced to target the `sm_61` architecture.
* `sm_61` corresponds to the NVIDIA Pascal architecture (e.g., GTX 1060, 1070, 1080), which was released around 2016 and is now considered obsolete.
* Hardcoding this old architecture means that if the code is run on modern GPUs (like Ampere, Ada Lovelace, or Hopper), it will completely miss out on modern hardware features such as Tensor Cores or advanced memory instructions. 
* **Recommendation:** Allow the CUDA architecture to be configured dynamically via a CMake option (e.g., `CMAKE_CUDA_ARCHITECTURES`) rather than hardcoding it.

### 3. Strict Intel SYCL Dependency (Vendor Lock-in)
The `CMakeLists.txt` is heavily coupled with the Intel SYCL compiler ecosystem (using `icpx`, `icx-cl`, or Intel's `intel/llvm` open-source fork). 
* While standard C++ SYCL is used, relying exclusively on Intel's specific flags and compiler paths creates a vendor lock-in risk. 
* This makes it significantly harder to port the project to alternative or standard compilers, or to other platforms like Apple Silicon or AMD ROCm environments without heavily modifying the build system.
