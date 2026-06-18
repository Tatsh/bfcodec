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

# deploy-rb validates rb packages against the goodrb.json database.
find_package(nlohmann_json CONFIG REQUIRED)

# deploy-rb processes packages on a thread pool.
find_package(Threads REQUIRED)

if(NOT APPLE AND NOT WIN32)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
elseif(WIN32)
  set(win_crypto_lib "advapi32")
endif()

string(TIMESTAMP YEAR "%Y")

add_executable(unjbt unjbt.cpp jbtcommon.cpp jbtarchive.cpp)
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

add_executable(jbt jbt.cpp jbtcommon.cpp jbtarchive.cpp)
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

add_executable(bfc bfc.cpp jbtcommon.cpp)
if(WIN32)
  set(ORIGINAL_FILENAME "bfc")
  configure_file("version.rc.in" bfc-version.rc @ONLY)
  target_sources(bfc PRIVATE bfc-version.rc)
endif()
target_link_libraries(bfc PRIVATE bfcodec argparse::argparse spdlog::spdlog)
if(NOT APPLE AND NOT WIN32)
  target_include_directories(bfc PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(bfc PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
  target_link_libraries(bfc PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(bfc PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")

add_executable(unbfc unbfc.cpp jbtcommon.cpp)
if(WIN32)
  set(ORIGINAL_FILENAME "unbfc")
  configure_file("version.rc.in" unbfc-version.rc @ONLY)
  target_sources(unbfc PRIVATE unbfc-version.rc)
endif()
target_link_libraries(unbfc PRIVATE bfcodec argparse::argparse spdlog::spdlog)
if(NOT APPLE AND NOT WIN32)
  target_include_directories(unbfc PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(unbfc PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
  target_link_libraries(unbfc PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(unbfc PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")

# deploy-rb builds a complete mulist from song packages and deploys it over SSH. It calls the
# bfcodec library directly for encryption and decryption and shells out to ssh/scp/rsync through the
# portable process helpers in jbtcommon, so it needs libzip and (for the info plist) libplist.
add_executable(deploy-rb deploy-rb.cpp jbtcommon.cpp jbtarchive.cpp)
if(WIN32)
  set(ORIGINAL_FILENAME "deploy-rb")
  configure_file("version.rc.in" deploy-rb-version.rc @ONLY)
  target_sources(deploy-rb PRIVATE deploy-rb-version.rc)
endif()
target_link_libraries(deploy-rb PRIVATE bfcodec argparse::argparse libzip::zip nlohmann_json::nlohmann_json spdlog::spdlog Threads::Threads ${plist_target})
if(plist_target)
  target_compile_definitions(deploy-rb PRIVATE HAVE_LIBPLIST)
endif()
if(NOT APPLE AND NOT WIN32)
  target_include_directories(deploy-rb PRIVATE ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(deploy-rb PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
  target_link_libraries(deploy-rb PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(deploy-rb PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")
if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  if(COREFOUNDATION_FRAMEWORK)
    target_link_libraries(deploy-rb PRIVATE ${COREFOUNDATION_FRAMEWORK})
    target_compile_definitions(deploy-rb PRIVATE HAVE_COREFOUNDATION)
  endif()
endif()

if(BFCODEC_APPLY_WARN_ALL)
  target_compile_options(unjbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
  target_compile_options(jbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
  target_compile_options(bfc PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
  target_compile_options(unbfc PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
  target_compile_options(deploy-rb PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

install(TARGETS unjbt jbt bfc unbfc deploy-rb
  DESTINATION ${CMAKE_INSTALL_BINDIR}
  RUNTIME_DEPENDENCY_SET jbt
)
# deploy-jbt is the same binary as deploy-rb; it defaults to the jbt package type when invoked under
# that name. Install it as a symbolic link where supported, and as a copy on Windows where symlinks
# need elevated privileges.
if(WIN32)
  install(PROGRAMS $<TARGET_FILE:deploy-rb>
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    RENAME deploy-jbt${CMAKE_EXECUTABLE_SUFFIX})
else()
  install(CODE "
    set(_bin_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    message(STATUS \"Installing: \${_bin_dir}/deploy-jbt -> deploy-rb\")
    file(REMOVE \"\${_bin_dir}/deploy-jbt\")
    file(CREATE_LINK deploy-rb \"\${_bin_dir}/deploy-jbt\" SYMBOLIC)
  ")
endif()
if(WIN32)
  get_filename_component(_compiler_bin_dir ${CMAKE_C_COMPILER} DIRECTORY)
  install(
    RUNTIME_DEPENDENCY_SET
    jbt
    PRE_EXCLUDE_REGEXES
    [=[api-ms-]=]
    [=[ext-ms-]=]
    [[kernel32\.dll]]
    POST_EXCLUDE_REGEXES
    [=[.*system32\/.*\.dll]=]
    DIRECTORIES
    ${CMAKE_INSTALL_PREFIX}/bin
    ${_compiler_bin_dir}
    $<TARGET_FILE_DIR:jbt>)
elseif(APPLE)
  if(EXISTS "/opt/local/lib")
    set(_macports_dir "/opt/local/lib")
  endif()
  if(EXISTS "/opt/homebrew/lib")
    set(_homebrew_dir "/opt/homebrew/lib")
  endif()
  install(
    RUNTIME_DEPENDENCY_SET
    jbt
    PRE_EXCLUDE_REGEXES
    "."
    PRE_INCLUDE_REGEXES
    "spdlog|plist|zip|zlib|zstd|lzma|bz2"
    DIRECTORIES
    ${CMAKE_INSTALL_PREFIX}/bin
    ${_macports_dir}
    ${_homebrew_dir})
endif()
