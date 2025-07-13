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
        src/util/Progress.cpp
        src/util/Progress.h
        src/titles.cpp
        src/titles.h
        src/headers/headerTree.cpp
        src/headers/headerTree.h
        src/util/structs.h
        src/markup/Whitespace.h
        src/markup/Comments.h
        src/markup/wikinodes/Header.h
        src/markup/wikinodes/HeaderParser.h
        src/markup/wikinodes/WikiLink.h
        src/markup/wikinodes/WikiLinkParser.h
        src/markup/WikiText.h
        src/markup/Markup.h
        src/markup/wikinodes/Template.h
        src/markup/wikinodes/TemplateParser.h
        src/markup/wikinodes/TemplateParser.cpp
        src/markup/MarkupParser.h
        src/markup/MarkupParser.cpp
        src/markup/tags/TaggedContent.h
        src1/handlers/TagHandler.h
        src1/elements/Tag.h
        src1/elements/WikiElement.h
        src1/elements/TaggedContent.h
        src1/factory/TagFactory.cpp
        src1/factory/TagFactory.h
        src1/parser/Parser.h
        src1/elements/Template.h
        src1/elements/Header.h
        src1/elements/WikiLink.h
        src1/elements/ExternalLink.h
        src1/elements/TextElement.h
        src1/parser/ParserError.h
        src1/parser/Parser.cpp
        src1/elements/TaggedContent.cpp)

target_link_libraries(${PROJECT_NAME} bz2 pugixml::pugixml)
