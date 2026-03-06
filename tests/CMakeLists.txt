include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(BUILD_GMOCK OFF CACHE BOOL "")
set(INSTALL_GTEST OFF CACHE BOOL "")
FetchContent_MakeAvailable(googletest)
list(APPEND CMAKE_MODULE_PATH ${googletest_SOURCE_DIR}/googletest/cmake)

enable_testing()

add_executable(bfcodec_test bfcodec_test.cpp)
target_link_libraries(bfcodec_test PRIVATE
  bfcodec_core
  gtest_main
)
target_include_directories(bfcodec_test PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(bfcodec_test PROPERTIES
  CXX_STANDARD 23
  CXX_EXTENSIONS OFF
)

if(BFCODEC_APPLY_WARN_ALL)
	target_compile_options(bfcodec_test PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

include(GoogleTest)
gtest_discover_tests(bfcodec_test)
