cmake_minimum_required(VERSION 3.28)
project(wiktparse)

set(CMAKE_CXX_STANDARD 23)

# wikilib: reading Wikimedia dumps (bz2, index, XML)
set(WIKILIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../wikilib CACHE PATH "wikilib source directory")
set(WIKILIB_BUILD_TESTS OFF)
set(WIKILIB_BUILD_EXAMPLES OFF)
add_subdirectory(${WIKILIB_DIR} wikilib EXCLUDE_FROM_ALL)

find_package(nlohmann_json REQUIRED)

# Dump access and Wikidata selection shared by wiktparse and tools
add_library(wiktdump STATIC
        src/dump/WikiDump.cpp
        src/dump/WikiDump.h
        src/util/Progress.cpp
        src/util/Progress.h
        src/titles.cpp
        src/titles.h
        src/WikiData.cpp
        src/WikiData.h
        src/WikiTerms.cpp
        src/WikiTerms.h
        src/util/textUtils.cpp
        src/util/textUtils.h
        src/wikitext/HeaderTerms.cpp
        src/wikitext/HeaderTerms.h
        src/wikitext/LuaData.cpp
        src/wikitext/LuaData.h)
target_include_directories(wiktdump PUBLIC src)
# Default location of rules files (res/wikidata_selection.tsv)
target_compile_definitions(wiktdump PRIVATE WIKTPARSE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
target_link_libraries(wiktdump PUBLIC wikilib::wikilib nlohmann_json::nlohmann_json)

# Creates wiktNeeded.jsonl from the Wikidata dump
add_executable(wiktneeded tools/wiktneeded.cpp)
target_link_libraries(wiktneeded wiktdump)

# Statistics of section headers of all Wiktionaries
add_executable(wiktheaders tools/wiktheaders.cpp)
target_link_libraries(wiktheaders wiktdump)

# Languages of English Wiktionary modules with Wikidata names, into SQLite
find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
add_executable(wiktlangs tools/wiktlangs.cpp)
target_link_libraries(wiktlangs wiktdump SQLite::SQLite3 CURL::libcurl)

# Term lists (languages, parts of speech, sections) from wiktheaders output
add_executable(wiktterms tools/wiktterms.cpp)
target_link_libraries(wiktterms wiktdump)

add_executable(${PROJECT_NAME} src/main.cpp
        src/headers/headers.cpp
        src/headers/headers.h
        src/wikitext/templates.cpp
        src/wikitext/templates.h
        src/headers/headerTree.cpp
        src/headers/headerTree.h
        src/util/structs.h
        src/markup/Whitespace.h
        src/markup/wikinodes/Header.h
        src/markup/wikinodes/HeaderParser.h
        src/markup/wikinodes/WikiLink.h
        src/markup/wikinodes/WikiLinkParser.h
        src/markup/Markup.h
        src/markup/wikinodes/Template.h
        src/markup/wikinodes/TemplateParser.h
        src/markup/wikinodes/TemplateParser.cpp
        src/markup/MarkupParser.h
        src/markup/MarkupParser.cpp
        src/markup/tags/TaggedContent.h
        src1/handlers/TagHandler.h
        src1/elements/Tag.h
        src1/elements/Tag.cpp
        src1/elements/WikiElement.h
        src1/elements/WikiElement.cpp
        src1/elements/TaggedContent.h
        src1/factory/TagFactory.cpp
        src1/factory/TagFactory.h
        src1/parser/Parser.h
        src1/elements/Template.h
        src1/elements/Template.cpp
        src1/elements/Header.h
        src1/elements/Header.cpp
        src1/elements/WikiLink.h
        src1/elements/WikiLink.cpp
        src1/elements/ExternalLink.h
        src1/elements/ExternalLink.cpp
        src1/elements/TextElement.h
        src1/elements/TextElement.cpp
        src1/parser/ParserError.h
        src1/parser/Parser.cpp
        src1/elements/TaggedContent.cpp
        src1/preprocessor/Preprocessor.cpp
        src1/preprocessor/Preprocessor.h)

target_link_libraries(${PROJECT_NAME} wiktdump)

add_subdirectory(tests)
