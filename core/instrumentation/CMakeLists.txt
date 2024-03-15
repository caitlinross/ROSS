set(headers
  st-instrumentation.h
  ross-lps/analysis-lp.h
)

target_sources(ROSS PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/st-instrumentation.c
  ${CMAKE_CURRENT_SOURCE_DIR}/st-stats-buffer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/st-sim-engine.c
  ${CMAKE_CURRENT_SOURCE_DIR}/st-event-trace.c
  ${CMAKE_CURRENT_SOURCE_DIR}/st-model-data.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ross-lps/analysis-lp.c
  ${CMAKE_CURRENT_SOURCE_DIR}/ross-lps/specialized-lps.c
)

target_include_directories(ROSS PRIVATE ${CMAKE_CURRENT_LIST_DIR})

install(FILES ${headers}
  DESTINATION include/instrumentation)
