cmake_minimum_required(VERSION 3.14)
project(rdf_to_graph CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

find_package(SQLite3 REQUIRED)
if(TARGET SQLite3::SQLite3)
  set(SQLITE_LIB SQLite3::SQLite3)
else()
  set(SQLITE_LIB SQLite::SQLite3)
endif()

add_executable(graph_viewer
  main.cpp
  module/rdf_graph_generator.cpp
  module/line_finder_v2.cpp
  module/log_util.cpp
)

target_include_directories(graph_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(graph_viewer PRIVATE -Wall -Wextra)
target_link_libraries(graph_viewer PRIVATE ${SQLITE_LIB})
