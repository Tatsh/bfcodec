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


find_package(PkgConfig REQUIRED)
set(plist_target "")
pkg_check_modules(LIBPLIST IMPORTED_TARGET GLOBAL libplist)
if(TARGET PkgConfig::LIBPLIST)
    set(plist_target PkgConfig::LIBPLIST)
endif()

find_package(libzip CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(NOT APPLE AND NOT WIN32)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
elseif(WIN32)
    set(win_crypto_lib "advapi32")
endif()

add_executable(unjbt unjbt.cpp jbtcommon.cpp)
target_link_libraries(unjbt PRIVATE bfcodec_core argparse::argparse libzip::zip spdlog::spdlog ${plist_target})
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
set_target_properties(unjbt PROPERTIES
    CXX_STANDARD 23
    CXX_EXTENSIONS OFF
)


if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    if(COREFOUNDATION_FRAMEWORK)
        target_link_libraries(unjbt PRIVATE ${COREFOUNDATION_FRAMEWORK})
        target_compile_definitions(unjbt PRIVATE HAVE_COREFOUNDATION)
    endif()
endif()

add_executable(jbt jbt.cpp jbtcommon.cpp)
target_link_libraries(jbt PRIVATE bfcodec_core argparse::argparse libzip::zip spdlog::spdlog)
if(NOT APPLE AND NOT WIN32)
    target_include_directories(jbt PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(jbt PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
elseif(WIN32)
    target_link_libraries(jbt PRIVATE ${win_crypto_lib})
endif()
target_compile_definitions(jbt PRIVATE BFCODEC_VERSION="${CMAKE_PROJECT_VERSION}")
set_target_properties(jbt PROPERTIES
    CXX_STANDARD 23
    CXX_EXTENSIONS OFF
)

if(BFCODEC_APPLY_WARN_ALL)
	target_compile_options(unjbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
	target_compile_options(jbt PRIVATE ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

install(TARGETS unjbt jbt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE.txt TYPE DOC)

if(WIN32)
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_DISPLAY_NAME "bfcodec")
    set(CPACK_NSIS_PACKAGE_NAME "bfcodec")
    set(CPACK_PACKAGE_EXECUTABLES "jbt;jbt" "unjbt;unjbt")
    set(CPACK_GENERATOR NSIS ZIP)
else()
    set(CPACK_GENERATOR ZIP)
endif()
set(CPACK_PACKAGE_VENDOR "bfcodec")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE.txt)
set(__cpack_system_name ${CMAKE_SYSTEM_NAME})
if(__cpack_system_name MATCHES "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(__cpack_system_name win64)
    else()
        set(__cpack_system_name win32)
    endif()
elseif(APPLE)
    if(CMAKE_APPLE_SILICON_PROCESSOR)
        set(__cpack_system_name Darwin-arm64)
    else()
        set(__cpack_system_name Darwin-x86_64)
    endif()
endif()
set(CPACK_PACKAGE_FILE_NAME
    ${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${__cpack_system_name}-${CMAKE_BUILD_TYPE})
set(CPACK_THREADS 0)
include(CPack)
