cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)
find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} main.cpp liner/Bz2Liner.cpp liner/AbstractBufferedLiner.cpp
        WikiChunker.cpp
        WikiChunker.h
        Xml.cpp
        Xml.h
        tags.cpp
        tags.h
        Index.cpp
        Index.h
        util/textUtils.cpp
        util/textUtils.h
        WikiFile.cpp
        WikiFile.h
        Comments.cpp
        Comments.h
        WikiName.h
        WikiData.cpp
        WikiData.h
        WikiName.cpp
        template/templates.cpp
        template/templates.h
        template/Template.cpp
        template/Template.h
        template/TemplateParser.cpp
        template/TemplateParser.h
        util/Progress.cpp
        util/Progress.h
        titles.cpp
        titles.h
        tagTree.cpp
        tagTree.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
