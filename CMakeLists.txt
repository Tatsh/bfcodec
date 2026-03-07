cmake_minimum_required(VERSION 3.16)
project(bfcodec VERSION 0.0.0 LANGUAGES C CXX)

include(CheckTypeSize)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_DOCS "Build documentation." OFF)
option(BUILD_DOCS_ONLY "Build documentation only." OFF)
option(BUILD_TESTS "Build unit tests." OFF)
option(BUILD_TOOLS "Build tools." ON)
option(COVERAGE "Enable code coverage." OFF)
option(CXX_WARN_ALL "Enable extra compiler warnings." OFF)
option(EXTRA_DOCS "Install extra documentation." OFF)
mark_as_advanced(CXX_WARN_ALL)

if(NOT BUILD_DOCS_ONLY)
  if(WIN32)
    check_type_size(nullptr_t STD_NULLPTR_T BUILTIN_TYPES_ONLY LANGUAGE C)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS
                            _UNICODE
                            HAVE_NULLPTR_T=$<IF:$<BOOL:${HAVE_STD_NULLPTR_T}>,1,0>
                            UNICODE
                            WIN32_LEAN_AND_MEAN)
  endif()

  add_compile_options($<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${COVERAGE}>>:--coverage>)
  add_link_options($<$<AND:$<CXX_COMPILER_ID:Clang,GNU>,$<BOOL:${COVERAGE}>>:--coverage>)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CXX_WARN_ALL)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      set(BFCODEC_APPLY_WARN_ALL TRUE)
      set(BFCODEC_WARN_ALL_C_FLAGS
        "$<IF:$<COMPILE_LANGUAGE:C>,-Weverything,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-declaration-after-statement,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-disabled-macro-expansion,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-padded,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-pre-c11-compat,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-reserved-id-macro,>"
        "$<IF:$<COMPILE_LANGUAGE:C>,-Wno-unsafe-buffer-usage,>")
      set(BFCODEC_WARN_ALL_CXX_FLAGS
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-fsafe-buffer-usage-suggestions,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Weverything,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-c++98-compat,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-c++98-compat-pedantic,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-exit-time-destructors,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-global-constructors,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-padded,>")
    endif()
  endif()

  if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()

  add_subdirectory(src)

  if(BUILD_TOOLS)
    add_subdirectory(tools)
    install(FILES
      man/jbt.1
      man/unjbt.1
      DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    )
  endif()
endif()

if(BUILD_DOCS OR BUILD_DOCS_ONLY)
  set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
  if(EXTRA_DOCS)
    set(DOXYGEN_GENERATE_HTML YES)
  endif()
  set(DOXYGEN_GENERATE_MAN YES)
  find_package(Doxygen REQUIRED)
  doxygen_add_docs(docs README.md include src ALL)
endif()

install(FILES README.md CHANGELOG.md SECURITY.md TYPE DOC)
install(FILES CITATION.cff DESTINATION ${CMAKE_INSTALL_DATADIR}/bfcodec)
if(BUILD_DOCS)
  install(FILES ${CMAKE_BINARY_DIR}/man/man3/bfcodec.h.3
    ${CMAKE_BINARY_DIR}/man/man3/bfcodecpp.h.3
    TYPE MAN)
endif()
if(EXTRA_DOCS)
  install(DIRECTORY ${CMAKE_BINARY_DIR}/html DESTINATION ${CMAKE_INSTALL_DATADIR}/bfcodec)
endif()
