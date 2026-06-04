cmake_minimum_required(VERSION 3.16)

project(fastdds_training_minimal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(fastcdr REQUIRED)
find_package(fastdds QUIET)
if(fastdds_FOUND)
    set(FASTDDS_LIBRARY fastdds)
else()
    find_package(fastrtps REQUIRED)
    set(FASTDDS_LIBRARY fastrtps)
endif()

find_program(FASTDDSGEN_EXECUTABLE NAMES fastddsgen fastddsgen.bat)
if(NOT FASTDDSGEN_EXECUTABLE)
    message(FATAL_ERROR "fastddsgen was not found. Install Fast DDS-Gen, then re-run cmake.")
endif()

set(IDL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/idl/HelloWorld.idl)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

set(GENERATED_SOURCES
    ${GENERATED_DIR}/HelloWorld.cxx
    ${GENERATED_DIR}/HelloWorldPubSubTypes.cxx
    ${GENERATED_DIR}/HelloWorldTypeObjectSupport.cxx
)

set(GENERATED_HEADERS
    ${GENERATED_DIR}/HelloWorld.hpp
    ${GENERATED_DIR}/HelloWorldCdrAux.hpp
    ${GENERATED_DIR}/HelloWorldCdrAux.ipp
    ${GENERATED_DIR}/HelloWorldPubSubTypes.hpp
    ${GENERATED_DIR}/HelloWorldTypeObjectSupport.hpp
)

add_custom_command(
    OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${FASTDDSGEN_EXECUTABLE} -replace -typeobject -d ${GENERATED_DIR} ${IDL_FILE}
    DEPENDS ${IDL_FILE}
    VERBATIM
)

add_executable(dds_demo
    src/dds_demo.cpp
    ${GENERATED_SOURCES}
)

target_include_directories(dds_demo PRIVATE ${GENERATED_DIR})
target_link_libraries(dds_demo PRIVATE ${FASTDDS_LIBRARY} fastcdr)
