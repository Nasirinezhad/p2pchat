cmake_minimum_required(VERSION 3.2.2)

project(peer)

include_directories(${PROJECT_SOURCE_DIR})
add_executable(app main.cpp)

install(TARGETS peer RUNTIME DESTINATION bin)