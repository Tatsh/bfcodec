add_library(bfcodec_core
  bfcodec.c
  bfcodec.cc
)

target_include_directories(bfcodec_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)


if(BFCODEC_APPLY_WARN_ALL)
  target_compile_options(bfcodec_core PRIVATE
    ${BFCODEC_WARN_ALL_C_FLAGS}
    ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

install(TARGETS bfcodec_core
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
  ${CMAKE_SOURCE_DIR}/include/bfcodec.h
  ${CMAKE_SOURCE_DIR}/include/bfcodecpp.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
