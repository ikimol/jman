# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# jman :: root :: CMakeLists.txt
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

cmake_minimum_required(VERSION 3.9 FATAL_ERROR)

# ------------------------------------------------------------------------------
# project options
# ------------------------------------------------------------------------------

set(JMAN_VERSION 1.0.0)

project(jman VERSION ${JMAN_VERSION})

message(STATUS "${PROJECT_NAME} version ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 14)

# ------------------------------------------------------------------------------
# build options
# ------------------------------------------------------------------------------

option(JMAN_BUILD_EXAMPLES "Enable to build the example targets" OFF)
option(JMAN_BUILD_TESTS "Enable to build the tests" OFF)
option(JMAN_BUILD_BENCHMARKS "Enable to build the benchmarks" OFF)
option(JMAN_BUILD_DOCS "Enable to build the documentation" OFF)

# ------------------------------------------------------------------------------
# dependencies
# ------------------------------------------------------------------------------

find_package(shard REQUIRED algorithm concurrency)

# ------------------------------------------------------------------------------
# create targets
# ------------------------------------------------------------------------------

set(JMAN_LIB_NAME jman)

add_library(${JMAN_LIB_NAME} INTERFACE)

target_include_directories(${JMAN_LIB_NAME} INTERFACE
                           $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                           $<INSTALL_INTERFACE:include>
                           )

target_link_libraries(${JMAN_LIB_NAME} INTERFACE shard::shard)

# add examples

if (JMAN_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

# add tests

if (JMAN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

# add documentation

if (JMAN_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
endif ()

# ------------------------------------------------------------------------------
# installation
# ------------------------------------------------------------------------------

# setup the install rules

set(JMAN_CONFIG_EXPORT_NAME "jman_config_export")

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.inl"
        )

install(TARGETS ${JMAN_LIB_NAME} EXPORT ${JMAN_CONFIG_EXPORT_NAME}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
