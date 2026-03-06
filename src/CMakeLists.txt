add_library(bfcodec_core
	bfcodec.c
	bfcodec.cc
)

target_include_directories(bfcodec_core PUBLIC
	$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(bfcodec_core PROPERTIES
	CXX_STANDARD 23
	CXX_EXTENSIONS OFF
)

install(TARGETS bfcodec_core
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
	${CMAKE_SOURCE_DIR}/include/bfcodec.h
	${CMAKE_SOURCE_DIR}/include/bfcodecpp.h
	DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
