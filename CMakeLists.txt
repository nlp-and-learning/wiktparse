cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)
find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} main.cpp Bz2Liner.cpp AbstractLiner.cpp
        xml.cpp
        xml.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
