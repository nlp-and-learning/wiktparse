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
        src/titles.cpp
        src/titles.h
        src/WikiData.cpp
        src/WikiData.h
        src/WikiTerms.cpp
        src/WikiTerms.h
        src/util/textUtils.cpp
        src/util/textUtils.h
        src/wikitext/HeaderTerms.cpp
        src/wikitext/HeaderTerms.h)
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
target_compile_definitions(wiktlangs PRIVATE WIKTPARSE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")

# Sample pages of every Wiktionary (water, dog, house, red, go) for the per-wiki configuration
# Wiktionary pages to JSONL, configured by res/wiktionary
add_executable(wikt2jsonl tools/wikt2jsonl.cpp)
target_compile_definitions(wikt2jsonl PRIVATE WIKTPARSE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
target_link_libraries(wikt2jsonl wiktdump SQLite::SQLite3)

add_executable(wiktsamples tools/wiktsamples.cpp)
target_link_libraries(wiktsamples wiktdump SQLite::SQLite3)

# Term lists (languages, parts of speech, sections) from wiktheaders output
add_executable(wiktterms tools/wiktterms.cpp)
target_link_libraries(wiktterms wiktdump)

# Linking English definitions to translation glosses (from my_c/wikitools/defglos)
add_library(defglos STATIC
        src/defglos/Samples.cpp
        src/defglos/Samples.h
        src/defglos/Similarity.cpp
        src/defglos/Similarity.h
        src/defglos/Matcher.cpp
        src/defglos/Matcher.h)
target_link_libraries(defglos PUBLIC wiktdump)

# Evaluates definition-gloss linking on res/defglos/samples.txt
add_executable(defglos_eval tools/defglos.cpp)
set_target_properties(defglos_eval PROPERTIES OUTPUT_NAME defglos)
target_compile_definitions(defglos_eval PRIVATE WIKTPARSE_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/res")
target_link_libraries(defglos_eval defglos)

add_executable(${PROJECT_NAME} src/main.cpp
        src/headers/headers.cpp
        src/headers/headers.h
        src/headers/headerTree.cpp
        src/headers/headerTree.h
        src/util/structs.h)

target_link_libraries(${PROJECT_NAME} wiktdump)

add_subdirectory(tests)
