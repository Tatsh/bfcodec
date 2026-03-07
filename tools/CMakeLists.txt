find_package(argparse CONFIG QUIET)
if(NOT TARGET argparse::argparse)
  find_path(ARGPARSE_INCLUDE_DIR NAMES argparse/argparse.hpp)
  if(ARGPARSE_INCLUDE_DIR)
    add_library(argparse::argparse INTERFACE IMPORTED)
    set_target_properties(argparse::argparse PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${ARGPARSE_INCLUDE_DIR}")
  else()
    include(FetchContent)
    FetchContent_Declare(argparse
      GIT_REPOSITORY https://github.com/p-ranav/argparse.git
      GIT_TAG v3.2
    )
    FetchContent_MakeAvailable(argparse)
  endif()
endif()


# Prefer vcpkg/CMake config for libplist (e.g. Visual C++ with vcpkg); fall back to PkgConfig.
# FORCE_PKGCONFIG_LIBPLIST skips config-based find and uses PkgConfig only.
set(plist_target "")
if(FORCE_PKGCONFIG_LIBPLIST)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBPLIST IMPORTED_TARGET GLOBAL libplist-2.0)
  if(TARGET PkgConfig::LIBPLIST)
    set(plist_target PkgConfig::LIBPLIST)
  endif()
else()
  find_package(libplist CONFIG QUIET)
  if(TARGET libplist::libplist)
    set(plist_target libplist::libplist)
  else()
    find_package(unofficial-libplist CONFIG QUIET)
    if(TARGET unofficial::libplist::libplist)
      set(plist_target unofficial::libplist::libplist)
    else()
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(LIBPLIST IMPORTED_TARGET GLOBAL libplist-2.0)
      if(TARGET PkgConfig::LIBPLIST)
        set(plist_target PkgConfig::LIBPLIST)
      endif()
    endif()
  endif()
endif()

find_package(libzip CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT APPLE AND NOT WIN32)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
elseif(WIN32)
  set(win_crypto_lib "advapi32")
endif()

string(TIMESTAMP YEAR "%Y")

add_executable(unjbt unjbt.cpp jbtcommon.cpp)
if(WIN32)
  set(ORIGINAL_FILENAME "unjbt")
  configure_file("version.rc.in" unjbt-version.rc @ONLY)
  target_sources(unjbt PRIVATE unjbt-version.rc)
endif()
target_link_libraries(unjbt PRIVATE bfcodec argparse::argparse libzip::zip spdlog::spdlog ${plist_target})
if(plist_target)
  target_compile_definitions(unjbt PRIVATE HAVE_LIBPLIST)
endif()
if(NOT APPLE AND NOT WIN32)
  target_include_directories(unjbt PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(unjbt PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
  target_link_libraries(unjbt PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(unjbt PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")

if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  if(COREFOUNDATION_FRAMEWORK)
    target_link_libraries(unjbt PRIVATE ${COREFOUNDATION_FRAMEWORK})
    target_compile_definitions(unjbt PRIVATE HAVE_COREFOUNDATION)
  endif()
endif()

add_executable(jbt jbt.cpp jbtcommon.cpp)
if(WIN32)
  set(ORIGINAL_FILENAME "jbt")
  configure_file("version.rc.in" jbt-version.rc @ONLY)
  target_sources(jbt PRIVATE jbt-version.rc)
endif()
target_link_libraries(jbt PRIVATE bfcodec argparse::argparse libzip::zip spdlog::spdlog)
if(NOT APPLE AND NOT WIN32)
  target_include_directories(jbt PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(jbt PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
  target_link_libraries(jbt PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(jbt PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")

if(BFCODEC_APPLY_WARN_ALL)
  target_compile_options(unjbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
  target_compile_options(jbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

install(TARGETS unjbt jbt
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  RUNTIME_DEPENDENCY_SET jbt
)
if(NOT LINUX)
  if(MINGW)
    get_filename_component(_mingw_bin_dir ${CMAKE_C_COMPILER} DIRECTORY)
    set(_mingw_lib_dir ${_mingw_bin_dir})
  endif()
  if(APPLE)
    if(EXISTS "/opt/local/lib")
      set(_macports_dir "/opt/local/lib")
    endif()
    if (EXISTS "/opt/homebrew/lib")
      set(_homebrew_dir "/opt/homebrew/lib")
    endif()
  endif()
  message(STATUS "Directories: ${CMAKE_INSTALL_PREFIX}/lib ${_mingw_lib_dir}")
  install(
    RUNTIME_DEPENDENCY_SET
    jbt
    PRE_EXCLUDE_REGEXES
    "."
    PRE_INCLUDE_REGEXES
    "gcc|stdc|spdlog|plist|zip|zlib|winpthread|zstd|lzma|bz2"
    DIRECTORIES
    ${CMAKE_INSTALL_PREFIX}/bin
    ${_mingw_bin_dir}
    ${_macports_dir}
    ${_homebrew_dir})
endif()
