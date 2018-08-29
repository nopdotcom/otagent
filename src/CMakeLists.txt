# Copyright (c) Stash Inc. 2018 All rights reserved

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}
)

set(MODULE_NAME otagent)

file(GLOB cxx-sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB cxx-headers "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_executable(${MODULE_NAME}
  ${cxx-sources}
  ${cxx-headers})
  
target_link_libraries(
  ${MODULE_NAME}
  PRIVATE
  Threads::Threads
  ${APP_SYSTEM_LIBRARIES}
  ${PROTOBUF_LITE_LIBRARIES}
  ${OPENTXS-PROTO_LIBRARIES}
  ${OPENTXS_LIBRARIES}
  ${Boost_SYSTEM_LIBRARIES}
  ${Boost_FILESYSTEM_LIBRARIES}
  ${Boost_PROGRAM_OPTIONS_LIBRARIES}
  )

install(TARGETS ${MODULE_NAME} DESTINATION bin)
