cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)
find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} main.cpp Bz2Liner.cpp AbstractBufferedLiner.cpp
        WikiChunker.cpp
        WikiChunker.h
        Xml.cpp
        Xml.h
        tags.cpp
        tags.h
        Index.cpp
        Index.h
        textUtils.cpp
        textUtils.h
        WikiFile.cpp
        WikiFile.h
        Comments.cpp
        Comments.h
        WikiName.h
        WikiData.cpp
        WikiData.h
        WikiName.cpp
        Templates.cpp
        Templates.h
        Template.cpp
        Template.h
        TemplateParser.cpp
        TemplateParser.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
