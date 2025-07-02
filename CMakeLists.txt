cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)
find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp src/liner/Bz2Liner.cpp src/liner/AbstractBufferedLiner.cpp
        src/WikiChunker.cpp
        src/WikiChunker.h
        src/xml/Xml.cpp
        src/xml/Xml.h
        src/tag/tags.cpp
        src/tag/tags.h
        src/reader/Index.cpp
        src/reader/Index.h
        src/util/textUtils.cpp
        src/util/textUtils.h
        src/reader/WikiFile.cpp
        src/reader/WikiFile.h
        src/xml/Comments.cpp
        src/xml/Comments.h
        src/reader/WikiName.h
        src/WikiData.cpp
        src/WikiData.h
        src/reader/WikiName.cpp
        src/template/templates.cpp
        src/template/templates.h
        src/template/Template.cpp
        src/template/Template.h
        src/template/TemplateParser.cpp
        src/template/TemplateParser.h
        src/util/Progress.cpp
        src/util/Progress.h
        src/titles.cpp
        src/titles.h
        src/tag/tagTree.cpp
        src/tag/tagTree.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
