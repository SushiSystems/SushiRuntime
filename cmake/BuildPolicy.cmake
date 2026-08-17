# BuildPolicy.cmake — the single entry point for this tree's compile policy.
#
# Single responsibility: compose the four policy targets and expose one function
# that applies them to a target. Every target in the tree — library, examples,
# tests — goes through sushiruntime_apply_build_policy() and therefore compiles
# under identical settings.
#
# The composition draws one line deliberately, at the library's boundary:
#
#   PRIVATE (this tree only)   sushiruntime_warnings
#                              sushiruntime_perf
#                              sushiruntime_sanitizers
#   PUBLIC  (exported)         sushiruntime_deterministic_fp
#
# Warnings are a review standard, architecture targeting is a choice about the
# machine a binary is built for, and a sanitizer is a debugging tool — none of
# the three is something an installed consumer should silently inherit, and
# inheriting -Werror or -march=native from a dependency is a well-known way to
# make a downstream build fail for reasons its author cannot see. The
# floating-point contract is the exception because the runtime's kernels are
# instantiated in consumer translation units: the guarantee is only true if the
# consumer compiles under it too (see Determinism.cmake).
#
# Applying this via a function rather than a directory-scope link_libraries() is
# what makes the line enforceable. link_libraries() lands in a target's link
# interface, so the policy targets would appear in the installed export set and
# the flags they carry would reach consumers — the opposite of the intent.

include("${CMAKE_CURRENT_LIST_DIR}/CompilerWarnings.cmake")   # -> sushiruntime_warnings
include("${CMAKE_CURRENT_LIST_DIR}/Optimization.cmake")       # -> sushiruntime_perf
include("${CMAKE_CURRENT_LIST_DIR}/Determinism.cmake")        # -> sushiruntime_deterministic_fp
include("${CMAKE_CURRENT_LIST_DIR}/Sanitizers.cmake")         # -> sushiruntime_sanitizers

# Applies the project's in-tree compile policy to one target.
#
# The floating-point contract is intentionally not applied here: the library
# links it PUBLIC (src/CMakeLists.txt) so everything downstream — examples,
# tests, and installed consumers alike — inherits exactly one copy of it
# through the sushiruntime target.
# The BUILD_INTERFACE wrappers are load-bearing, not decoration. A PRIVATE link
# is still recorded in the target's INTERFACE_LINK_LIBRARIES as $<LINK_ONLY:...>
# — CMake has to, or a static library's consumers would not link its private
# dependencies — so install(EXPORT) would see the library's interface naming
# three non-imported targets that no export set contains, and refuse to export at
# all. That is the same leak the function-instead-of-link_libraries() choice above
# addresses, one level deeper: keeping the flags off consumers is not enough if
# their target names still appear in the exported interface. BUILD_INTERFACE
# resolves to the target while building this tree and to nothing in the install
# interface, which is exactly what these three are.
function(sushiruntime_apply_build_policy target)
    target_link_libraries(${target} PRIVATE
        "$<BUILD_INTERFACE:sushiruntime_warnings>"
        "$<BUILD_INTERFACE:sushiruntime_perf>"
        "$<BUILD_INTERFACE:sushiruntime_sanitizers>"
    )
endfunction()
