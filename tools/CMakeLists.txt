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


find_package(plist CONFIG QUIET)
set(plist_target "")
if(TARGET plist::plist)
    set(plist_target plist::plist)
    add_compile_definitions(HAVE_LIBPLIST)
endif()

find_package(libzip CONFIG REQUIRED)
set(libzip_target "")
if(TARGET libzip::zip)
    set(libzip_target libzip::zip)
elseif(TARGET libzip::zip_shared)
    set(libzip_target libzip::zip_shared)
elseif(TARGET libzip::zip_static)
    set(libzip_target libzip::zip_static)
else()
    message(FATAL_ERROR "Could not find a libzip target (tried libzip::zip, libzip::zip_shared, libzip::zip_static)")
endif()

add_executable(bfcodec bfcodec.cpp)
target_link_libraries(bfcodec PRIVATE bfcodec_core argparse::argparse)
set_target_properties(bfcodec PROPERTIES
    CXX_STANDARD 23
    CXX_EXTENSIONS OFF
)

add_executable(unjbt unjbt.cpp)
target_link_libraries(unjbt PRIVATE bfcodec_core argparse::argparse ${libzip_target} ${plist_target})
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

add_executable(jbt jbt.cpp)
target_link_libraries(jbt PRIVATE bfcodec_core argparse::argparse ${libzip_target})
set_target_properties(jbt PROPERTIES
    CXX_STANDARD 23
    CXX_EXTENSIONS OFF
)

install(TARGETS bfcodec unjbt jbt
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
