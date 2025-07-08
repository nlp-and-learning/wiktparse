cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(tests)

find_package(pugixml REQUIRED)

add_executable(${PROJECT_NAME} src/main.cpp src/liner/Bz2Liner.cpp src/liner/AbstractBufferedLiner.cpp
        src/WikiChunker.cpp
        src/WikiChunker.h
        src/xml/Xml.cpp
        src/xml/Xml.h
        src/headers/headers.cpp
        src/headers/headers.h
        src/reader/Index.cpp
        src/reader/Index.h
        src/util/textUtils.cpp
        src/util/textUtils.h
        src/reader/WikiFile.cpp
        src/reader/WikiFile.h
        src/reader/WikiName.h
        src/WikiData.cpp
        src/WikiData.h
        src/reader/WikiName.cpp
        src/wikitext/templates.cpp
        src/wikitext/templates.h
        src/wikitext/old_Template.cpp
        src/wikitext/old_Template.h
        src/wikitext/old_TemplateParser.cpp
        src/wikitext/old_TemplateParser.h
        src/util/Progress.cpp
        src/util/Progress.h
        src/titles.cpp
        src/titles.h
        src/headers/headerTree.cpp
        src/headers/headerTree.h
        src/util/structs.h
        src/wikitext/old_text.h
        src/wikitext/old_text.cpp
        src/markup/Whitespace.h
        src/markup/Comments.h
        src/markup/wikitext/Header.h
        src/markup/wikitext/HeaderParser.h)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
