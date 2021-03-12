cmake_minimum_required(VERSION 3.10)

project(png)

set(CMAKE_CXX_STANDARD 20)

file(GLOB HEADERS CONFIGURE_DEPENDS *.h)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

source_group(Headers FILES ${HEADERS})
source_group(Sources FILES ${SOURCES})


add_executable(png ${HEADERS} ${SOURCES})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT png)