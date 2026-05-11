cmake_minimum_required(VERSION 3.28)
project(
  bfcodec
  VERSION 0.0.6
  LANGUAGES C CXX
  DESCRIPTION "Tools and a C/C++ library to manipulate BFCodec-encrypted content"
  HOMEPAGE_URL "https://github.com/Tatsh/bfcodec")

include(CheckTypeSize)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 17)
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
option(FORCE_PKGCONFIG_LIBPLIST "Use PkgConfig to find libplist (skip CMake config/vcpkg)." OFF)
mark_as_advanced(CXX_WARN_ALL)

if(NOT BUILD_DOCS_ONLY)
  if(WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS
                            _UNICODE
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
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-covered-switch-default,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-exit-time-destructors,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-global-constructors,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-padded,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-unsafe-buffer-usage,>"
        "$<IF:$<COMPILE_LANGUAGE:CXX>,-Wno-unsafe-buffer-usage-in-container,>")
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

if(WIN32)
  set(CPACK_GENERATOR NSIS ZIP)
  set(DISPLAY_NAME "BFCodec")
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/extra.nsh.in ${CMAKE_BINARY_DIR}/extra.nsh @ONLY)
  string(REPLACE "/" "\\\\" CMAKE_BINARY_DIR_ESCAPED ${CMAKE_BINARY_DIR})
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
      "  !include \\\"${CMAKE_BINARY_DIR_ESCAPED}\\\\extra.nsh\\\"")
  set(CPACK_NSIS_DEFINES "RequestExecutionLevel user
!define /date CURRENT_YEAR \\\"%Y\\\"
VIProductVersion \\\"${PROJECT_VERSION}.0\\\"
VIAddVersionKey \\\"ProductName\\\" \\\"${DISPLAY_NAME}\\\"
VIAddVersionKey \\\"CompanyName\\\" \\\"Tatsh\\\"
VIAddVersionKey \\\"FileDescription\\\" \\\"${DISPLAY_NAME} installer.\\\"
VIAddVersionKey \\\"FileVersion\\\" \\\"${PROJECT_VERSION}\\\"
VIAddVersionKey \\\"ProductVersion\\\" \\\"${PROJECT_VERSION}.0\\\"
VIAddVersionKey \\\"LegalCopyright\\\" \\\"Copyright © \\\${CURRENT_YEAR} Tatsh.\\\"
VIAddVersionKey \\\"Comments\\\" \\\"${PROJECT_HOMEPAGE_URL}\\\"")
  set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
  set(CPACK_NSIS_CONTACT "https://bsky.app/profile/tatsh.bsky.social")
  # set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\${CMAKE_PROJECT_NAME}.exe")
  set(CPACK_PACKAGE_EXECUTABLES "${CMAKE_PROJECT_NAME};${DISPLAY_NAME}")
  set(CPACK_CREATE_DESKTOP_LINKS "${CMAKE_PROJECT_NAME};${DISPLAY_NAME}")
  set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
  set(CPACK_NSIS_DISPLAY_NAME ${DISPLAY_NAME})
  set(CPACK_NSIS_PACKAGE_NAME ${DISPLAY_NAME})
  set(CPACK_NSIS_MODIFY_PATH ON)
  set(CPACK_NSIS_URL_INFO_ABOUT ${PROJECT_HOMEPAGE_URL}/issues) # Support link in 'Programs and
                                                                # Features'.
  set(CPACK_PACKAGE_INSTALL_DIRECTORY ${DISPLAY_NAME})
else()
  set(CPACK_GENERATOR ZIP)
endif()
set(CPACK_PACKAGE_VENDOR Tatsh)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE.txt)
if(NOT CPACK_SYSTEM_NAME)
  set(__cpack_system_name ${CMAKE_SYSTEM_NAME})
  set(__windows_bitness "32")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(__windows_bitness "64")
  endif()
  if(__cpack_system_name MATCHES "Windows")
    if(MSVC)
      set(__cpack_win_toolset "msvc")
      set(__cpack_system_name "${__cpack_win_toolset}${__windows_bitness}")
    elseif(MINGW)
      if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64|arm64"
          OR CMAKE_C_COMPILER MATCHES "clangarm64|clang-aarch64"
          OR CMAKE_CXX_COMPILER MATCHES "clangarm64|clang-aarch64")
        set(__cpack_system_name "clangarm64")
      elseif(CMAKE_C_COMPILER MATCHES "ucrt" OR CMAKE_CXX_COMPILER MATCHES "ucrt")
        set(__cpack_system_name "ucrt${__windows_bitness}")
      else()
        set(__cpack_system_name "mingw${__windows_bitness}")
      endif()
    else()
      set(__cpack_system_name "win${__windows_bitness}")
    endif()
  elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
        OR (CMAKE_OSX_ARCHITECTURES AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
      set(__cpack_system_name "Darwin-arm64")
    else()
      set(__cpack_system_name "Darwin-x86_64")
    endif()
  else()
    set(__cpack_system_name "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  endif()
  set(CPACK_SYSTEM_NAME ${__cpack_system_name})
endif()
set(CPACK_THREADS 0)
if(CMAKE_CONFIGURATION_TYPES)
  set(CPACK_PACKAGE_FILE_NAME
      "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}-\${CPACK_BUILD_CONFIG}")
else()
  set(CPACK_PACKAGE_FILE_NAME
      ${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${CPACK_SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
endif()
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR};${CMAKE_MODULE_PATH}")
set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
include(CPack)
