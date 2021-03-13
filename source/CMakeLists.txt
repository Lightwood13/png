cmake_minimum_required(VERSION 3.10)

project(png)

set(CMAKE_CXX_STANDARD 20)
set(SFML_STATIC_LIBRARIES true)
set(SFML_DIR D:/lib/SFML-2.5.1/lib/cmake/SFML)

file(GLOB HEADERS CONFIGURE_DEPENDS *.h)
file(GLOB SOURCES CONFIGURE_DEPENDS *.cpp)

source_group(Headers FILES ${HEADERS})
source_group(Sources FILES ${SOURCES})

find_package(SFML 2.5 COMPONENTS graphics window system REQUIRED)
add_executable(png ${HEADERS} ${SOURCES})
target_link_libraries(png sfml-graphics sfml-system sfml-window)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT png)