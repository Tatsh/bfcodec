add_library(bfcodec_core
	bfcodec.c
	BFCodec.cpp
)

target_include_directories(bfcodec_core PUBLIC
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(bfcodec_core PROPERTIES
	CXX_STANDARD 23
	CXX_EXTENSIONS OFF
)

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
			GIT_TAG v2.9
		)
		FetchContent_MakeAvailable(argparse)
	endif()
endif()

add_executable(bfcodec main.cpp)
target_link_libraries(bfcodec PRIVATE bfcodec_core argparse::argparse)
set_target_properties(bfcodec PROPERTIES
	CXX_STANDARD 23
	CXX_EXTENSIONS OFF
)

install(TARGETS bfcodec_core
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS bfcodec
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
	${CMAKE_SOURCE_DIR}/include/bfcodec.h
	${CMAKE_SOURCE_DIR}/include/BFCodec.hpp
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
