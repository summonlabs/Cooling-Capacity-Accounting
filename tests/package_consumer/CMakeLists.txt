# An independent downstream consumer of the installed Cooling Capacity
# Accounting package. This project is deliberately NOT part of the parent build:
# it configures only against an installed prefix through find_package, so it
# proves that the *installed* package is usable rather than the build tree.

cmake_minimum_required(VERSION 3.20)

project(CoolingCapacityAccountingPackageConsumer
        VERSION 1.0.0
        DESCRIPTION "Out-of-tree consumer of the installed Cooling Capacity Accounting package"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CoolingCapacityAccounting 1.0 REQUIRED CONFIG)

add_executable(cca_package_consumer main.cpp)
target_link_libraries(cca_package_consumer
  PRIVATE CoolingCapacityAccounting::cooling_capacity_accounting)

if(MSVC)
  target_compile_options(cca_package_consumer PRIVATE /W4 /permissive- /utf-8 /WX)
else()
  target_compile_options(cca_package_consumer PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror)
endif()

message(STATUS "consumer configured against CoolingCapacityAccounting "
               "${CoolingCapacityAccounting_VERSION}")
