set(headers
  ${GVT}.h
)

target_sources(ROSS PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/${GVT}.c
)

target_include_directories(ROSS PRIVATE ${CMAKE_CURRENT_LIST_DIR})

install(FILES ${headers}
  DESTINATION include/gvt)

