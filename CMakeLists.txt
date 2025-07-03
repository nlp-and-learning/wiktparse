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
        src/wikitext/templates.cpp
        src/wikitext/templates.h
        src/wikitext/Template.cpp
        src/wikitext/Template.h
        src/wikitext/TemplateParser.cpp
        src/wikitext/TemplateParser.h
        src/util/Progress.cpp
        src/util/Progress.h
        src/titles.cpp
        src/titles.h
        src/tag/tagTree.cpp
        src/tag/tagTree.h
        src/util/structs.h
        src/wikitext/text.h
        src/wikitext/text.cpp)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
