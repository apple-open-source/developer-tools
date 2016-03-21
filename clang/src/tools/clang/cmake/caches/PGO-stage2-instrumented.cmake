# Apple-Internal
# Include Apple-stage2 to get the right settings for the instrumented build
include(${CMAKE_CURRENT_LIST_DIR}/Apple-stage2.cmake)

set(CMAKE_BUILD_TYPE RELEASE CACHE STRING "")
set(CLANG_ENABLE_BOOTSTRAP ON CACHE BOOL "")
set(LLVM_BUILD_EXTERNAL_COMPILER_RT ON CACHE BOOL "")

set(CLANG_BOOTSTRAP_TARGETS check-all check-llvm check-clang test-suite CACHE STRING "")

set(CLANG_BOOTSTRAP_CMAKE_ARGS
  -C ${CMAKE_CURRENT_LIST_DIR}/PGO-stage2.cmake
  CACHE STRING "")
