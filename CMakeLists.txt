cmake_minimum_required(VERSION 3.16)
project(bfcodec VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(BUILD_TOOLS "Build tools (jbt, unjbt)" ON)
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

add_subdirectory(src)

if(BUILD_TOOLS)
	add_subdirectory(tools)
endif()
