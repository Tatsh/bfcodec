cmake_minimum_required(VERSION 3.16)
project(bfcodec VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_DOCS_ONLY "Build documentation only" OFF)
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_TOOLS "Build tools (jbt, unjbt, bfcodec)" ON)
option(COVERAGE "Enable code coverage" OFF)
option(CXX_WARN_ALL "Enable extra compiler warnings (Debug + Clang only)" OFF)
option(EXTRA_DOCS "Install extra documentation (README, LICENSE)" OFF)
mark_as_advanced(CXX_WARN_ALL)

if(NOT BUILD_DOCS_ONLY)
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
			man/bfcodec.1
			man/jbt.1
			man/unjbt.1
			DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
		)
	endif()
endif()

if(BUILD_DOCS OR BUILD_DOCS_ONLY)
	set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_SOURCE_DIR}/README.md")
	set(DOXYGEN_GENERATE_HTML YES)
	set(DOXYGEN_GENERATE_MAN NO)
	find_package(Doxygen REQUIRED)
	doxygen_add_docs(docs README.md include src ALL)
endif()

if(EXTRA_DOCS)
	install(FILES README.md LICENSE.txt TYPE DOC)
endif()
