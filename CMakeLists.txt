# Cooling Capacity Accounting - build, test, install and packaging rules.
#
# Copyright 2026 Summon Software Labs.
# Licensed under the Apache License, Version 2.0.
#
# Everything here is target based: no directory-global flags are set, so the
# library, the tools, the examples, the benchmarks and the tests each carry
# exactly the options they need, and nothing this project uses for itself can
# leak into the installed package.

cmake_minimum_required(VERSION 3.20)

project(CoolingCapacityAccounting
        VERSION 1.0.0
        DESCRIPTION "Constituent cooling-capacity accounting for the Data Center Control Plane"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(CCA_BUILD_TESTS "Build the Cooling Capacity Accounting test suite" ON)
option(CCA_BUILD_TOOLS "Build the Cooling Capacity Accounting command line tool" ON)
option(CCA_BUILD_EXAMPLES "Build the Cooling Capacity Accounting example programs" ON)
option(CCA_BUILD_BENCHMARKS "Build the Cooling Capacity Accounting benchmarks" ON)
option(CCA_WARNINGS_AS_ERRORS "Treat every compiler warning as an error" ON)
option(CCA_SANITIZERS "Build with AddressSanitizer, and UndefinedBehaviorSanitizer on GCC and Clang" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckCXXCompilerFlag)
include(CTest)

set(CCA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/CoolingCapacityAccounting"
    CACHE STRING "Install destination of the Cooling Capacity Accounting CMake package files")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Applies the project build policy to one target. Every entry is PRIVATE, so
# none of it is part of the installed package.
function(cca_configure_target target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8 /EHsc)
    if(CCA_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/RTC1>)
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
    target_compile_options(${target} PRIVATE
      $<$<CONFIG:Release>:/O2>
      $<$<CONFIG:Release>:/Gy>
      $<$<CONFIG:Release>:/Gw>)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    if(CCA_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
  if(CCA_SANITIZERS)
    target_compile_options(${target} PRIVATE ${CCA_SANITIZER_COMPILE_OPTIONS})
    target_link_options(${target} PRIVATE ${CCA_SANITIZER_LINK_OPTIONS})
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------

if(CCA_SANITIZERS)
  if(MSVC)
    check_cxx_compiler_flag("/fsanitize=address" CCA_COMPILER_HAS_ASAN)
    if(NOT CCA_COMPILER_HAS_ASAN)
      message(FATAL_ERROR
        "CCA_SANITIZERS=ON cannot be used with ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}: a program built with /fsanitize=address does not "
        "compile and link here. Either the toolset predates AddressSanitizer support "
        "(Visual Studio 2019 16.9) or the optional C++ AddressSanitizer component is not "
        "installed. Reconfigure with -DCCA_SANITIZERS=OFF or install that component.")
    endif()
    set(CCA_SANITIZER_COMPILE_OPTIONS /fsanitize=address /Zi)
    set(CCA_SANITIZER_LINK_OPTIONS /fsanitize=address)
    message(STATUS "CoolingCapacityAccounting: AddressSanitizer enabled (MSVC)")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    check_cxx_compiler_flag("-fsanitize=address,undefined" CCA_COMPILER_HAS_ASAN_UBSAN)
    if(NOT CCA_COMPILER_HAS_ASAN_UBSAN)
      message(FATAL_ERROR
        "CCA_SANITIZERS=ON cannot be used with ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION}: the sanitizer runtime is unavailable. "
        "Reconfigure with -DCCA_SANITIZERS=OFF.")
    endif()
    set(CCA_SANITIZER_COMPILE_OPTIONS -fsanitize=address,undefined -fno-omit-frame-pointer)
    set(CCA_SANITIZER_LINK_OPTIONS -fsanitize=address,undefined)
    message(STATUS "CoolingCapacityAccounting: AddressSanitizer and UndefinedBehaviorSanitizer enabled")
  else()
    message(FATAL_ERROR
      "CCA_SANITIZERS=ON is supported with MSVC (/fsanitize=address) and with GCC or Clang "
      "(-fsanitize=address,undefined); the configured compiler is "
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}. "
      "Reconfigure with -DCCA_SANITIZERS=OFF.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

set(CCA_PUBLIC_HEADERS
  include/cooling_capacity_accounting/accounting.hpp
  include/cooling_capacity_accounting/canonical.hpp
  include/cooling_capacity_accounting/clock.hpp
  include/cooling_capacity_accounting/contribution.hpp
  include/cooling_capacity_accounting/cooling_capacity_accounting.hpp
  include/cooling_capacity_accounting/digest.hpp
  include/cooling_capacity_accounting/domain.hpp
  include/cooling_capacity_accounting/engine.hpp
  include/cooling_capacity_accounting/errors.hpp
  include/cooling_capacity_accounting/evidence.hpp
  include/cooling_capacity_accounting/ids.hpp
  include/cooling_capacity_accounting/interchange.hpp
  include/cooling_capacity_accounting/ledger.hpp
  include/cooling_capacity_accounting/limits.hpp
  include/cooling_capacity_accounting/measure.hpp
  include/cooling_capacity_accounting/policy.hpp
  include/cooling_capacity_accounting/scope.hpp
  include/cooling_capacity_accounting/snapshot.hpp
  include/cooling_capacity_accounting/store.hpp
  include/cooling_capacity_accounting/text.hpp
  include/cooling_capacity_accounting/units.hpp
  include/cooling_capacity_accounting/version.hpp
  include/cooling_capacity_accounting/wide.hpp)

set(CCA_SOURCES
  src/accounting.cpp
  src/canonical.cpp
  src/clock.cpp
  src/contribution.cpp
  src/digest.cpp
  src/domain.cpp
  src/engine.cpp
  src/errors.cpp
  src/evidence.cpp
  src/file_lock.cpp
  src/ids.cpp
  src/interchange.cpp
  src/ledger.cpp
  src/limits.cpp
  src/measure.cpp
  src/policy.cpp
  src/snapshot.cpp
  src/store.cpp
  src/text.cpp
  src/units.cpp
  src/validation.cpp
  src/version.cpp
  src/wide.cpp)

add_library(cooling_capacity_accounting ${CCA_SOURCES})
add_library(CoolingCapacityAccounting::cooling_capacity_accounting ALIAS cooling_capacity_accounting)

target_include_directories(cooling_capacity_accounting
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(cooling_capacity_accounting PUBLIC cxx_std_20)
set_target_properties(cooling_capacity_accounting PROPERTIES
  OUTPUT_NAME cooling_capacity_accounting
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

cca_configure_target(cooling_capacity_accounting)

# ---------------------------------------------------------------------------
# Tools, examples and benchmarks
# ---------------------------------------------------------------------------

function(cca_add_console_target target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE CoolingCapacityAccounting::cooling_capacity_accounting)
  cca_configure_target(${target})
endfunction()

if(CCA_BUILD_TOOLS)
  cca_add_console_target(cca tools/cca_cli.cpp)
endif()

set(CCA_EXAMPLE_SOURCES
  examples/01_scope_rollup.cpp
  examples/02_unknown_is_not_zero.cpp
  examples/03_derates_and_degradation.cpp
  examples/04_redundancy_and_reserve.cpp
  examples/05_substitution_and_mutual_exclusion.cpp
  examples/06_apportioned_shared_plant.cpp
  examples/07_persistence_and_recovery.cpp
  examples/08_interchange_and_canonical.cpp)

if(CCA_BUILD_EXAMPLES)
  foreach(source IN LISTS CCA_EXAMPLE_SOURCES)
    string(SUBSTRING "${source}" 9 2 CCA_EXAMPLE_NUMBER)
    cca_add_console_target(cca_example_${CCA_EXAMPLE_NUMBER} ${source})
  endforeach()
endif()

if(CCA_BUILD_BENCHMARKS)
  cca_add_console_target(cca_benchmark benchmarks/benchmark_accounting.cpp)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

set(CCA_TEST_SOURCES
  tests/test_adversarial.cpp
  tests/test_apportionment.cpp
  tests/test_canonical.cpp
  tests/test_closure.cpp
  tests/test_concurrency.cpp
  tests/test_corruption.cpp
  tests/test_crash.cpp
  tests/test_derates.cpp
  tests/test_digest.cpp
  tests/test_engine.cpp
  tests/test_errors.cpp
  tests/test_evidence.cpp
  tests/test_generations.cpp
  tests/test_ids.cpp
  tests/test_interchange.cpp
  tests/test_limits.cpp
  tests/test_multiprocess.cpp
  tests/test_mutual_exclusion.cpp
  tests/test_paths.cpp
  tests/test_persistence.cpp
  tests/test_policy.cpp
  tests/test_property.cpp
  tests/test_redundancy.cpp
  tests/test_restart.cpp
  tests/test_rollup.cpp
  tests/test_text.cpp
  tests/test_units.cpp
  tests/test_validation_precedence.cpp
  tests/test_version.cpp
  tests/test_wide.cpp)

if(CCA_BUILD_TESTS)
  add_executable(cca_tests
    ${CCA_TEST_SOURCES}
    tests/support/test_harness.cpp
    tests/support/test_main.cpp
    tests/support/test_process.cpp
    tests/support/reference_model.cpp
    tests/support/fixtures.cpp)
  target_link_libraries(cca_tests PRIVATE CoolingCapacityAccounting::cooling_capacity_accounting)
  target_include_directories(cca_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests/support)
  cca_configure_target(cca_tests)

  # The test binary re-executes itself to obtain real independent operating
  # system processes for the writer-lock, restart and crash tests.
  add_test(NAME cca.tests COMMAND cca_tests)

  if(CCA_BUILD_EXAMPLES)
    foreach(source IN LISTS CCA_EXAMPLE_SOURCES)
      string(SUBSTRING "${source}" 9 2 CCA_EXAMPLE_NUMBER)
      add_test(NAME cca.example.${CCA_EXAMPLE_NUMBER} COMMAND cca_example_${CCA_EXAMPLE_NUMBER})
    endforeach()
  endif()

  if(CCA_BUILD_TOOLS)
    add_test(NAME cca.cli.selfcheck COMMAND cca ${CMAKE_CURRENT_BINARY_DIR}/cli-selfcheck --self-check)
    add_test(NAME cca.cli.scenario COMMAND cca ${CMAKE_CURRENT_BINARY_DIR}/cli-scenario --scenario)
  endif()

  if(CCA_BUILD_BENCHMARKS)
    add_test(NAME cca.benchmark.smoke COMMAND cca_benchmark --quick)
  endif()
endif()

# ---------------------------------------------------------------------------
# Install and package
# ---------------------------------------------------------------------------

install(TARGETS cooling_capacity_accounting
  EXPORT CoolingCapacityAccountingTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${CCA_PUBLIC_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cooling_capacity_accounting)

if(CCA_BUILD_TOOLS)
  install(TARGETS cca RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(EXPORT CoolingCapacityAccountingTargets
  FILE CoolingCapacityAccountingTargets.cmake
  NAMESPACE CoolingCapacityAccounting::
  DESTINATION ${CCA_INSTALL_CMAKEDIR})

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CoolingCapacityAccountingConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CoolingCapacityAccountingConfig.cmake
  INSTALL_DESTINATION ${CCA_INSTALL_CMAKEDIR})

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/CoolingCapacityAccountingConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CoolingCapacityAccountingConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CoolingCapacityAccountingConfigVersion.cmake
  DESTINATION ${CCA_INSTALL_CMAKEDIR})

install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/CoolingCapacityAccounting)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "CoolingCapacityAccounting ${PROJECT_VERSION}")
message(STATUS "  compiler            : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  build type          : ${CMAKE_BUILD_TYPE}")
message(STATUS "  warnings as errors  : ${CCA_WARNINGS_AS_ERRORS}")
message(STATUS "  sanitizers          : ${CCA_SANITIZERS}")
message(STATUS "  tests/tools/examples: ${CCA_BUILD_TESTS}/${CCA_BUILD_TOOLS}/${CCA_BUILD_EXAMPLES}")
