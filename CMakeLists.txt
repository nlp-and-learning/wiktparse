cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)
find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} main.cpp liner/Bz2Liner.cpp liner/AbstractBufferedLiner.cpp
        WikiChunker.cpp
        WikiChunker.h
        xml/Xml.cpp
        xml/Xml.h
        tag/tags.cpp
        tag/tags.h
        reader/Index.cpp
        reader/Index.h
        util/textUtils.cpp
        util/textUtils.h
        reader/WikiFile.cpp
        reader/WikiFile.h
        xml/Comments.cpp
        xml/Comments.h
        reader/WikiName.h
        WikiData.cpp
        WikiData.h
        reader/WikiName.cpp
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
        tag/tagTree.cpp
        tag/tagTree.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
