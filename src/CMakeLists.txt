add_library(bfcodec
  bfcodec.c
  bfcodec.cc
)

target_include_directories(bfcodec PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Hide all symbols by default in the DSO; only BFCODEC_API (see bfcodec_export.h) is exported.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(bfcodec PRIVATE -fvisibility=hidden)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(bfcodec PRIVATE -fvisibility=hidden)
endif()

if(BUILD_SHARED_LIBS)
  target_compile_definitions(bfcodec PRIVATE bfcodec_EXPORTS)
endif()

if(BFCODEC_APPLY_WARN_ALL)
  target_compile_options(bfcodec PRIVATE
    ${BFCODEC_WARN_ALL_C_FLAGS}
    ${BFCODEC_WARN_ALL_CXX_FLAGS})
endif()

install(TARGETS bfcodec
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
  ${CMAKE_SOURCE_DIR}/include/bfcodec.h
  ${CMAKE_SOURCE_DIR}/include/bfcodecpp.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
