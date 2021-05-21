cmake_minimum_required(VERSION 3.2.2)

project(tracker)

include_directories(${PROJECT_SOURCE_DIR}/lib)
add_executable(app main.cpp)

install(TARGETS tracker RUNTIME DESTINATION bin)