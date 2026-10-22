# wiktbrowse: browser of wikt2jsonl output. A separate project, so that
# wiktparse itself needs neither Qt nor an editor:
#   cmake -S browser -B browser/build -DCMAKE_BUILD_TYPE=Release && cmake --build browser/build
cmake_minimum_required(VERSION 3.24)
project(wiktbrowse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(nlohmann_json REQUIRED)
find_package(BZip2 REQUIRED)

# QCodeEditor (MIT): only its sources, built here with Qt6 (its own CMake wants Qt5)
include(FetchContent)
FetchContent_Declare(qcodeeditor
        GIT_REPOSITORY https://github.com/cpeditor/QCodeEditor.git
        GIT_TAG ed1196a91dd6415c5ad6d0e85a90630e9b3b9f6c
        SOURCE_SUBDIR none)
FetchContent_MakeAvailable(qcodeeditor)
file(GLOB QCE_SOURCES ${qcodeeditor_SOURCE_DIR}/src/internal/*.cpp ${qcodeeditor_SOURCE_DIR}/include/internal/*.hpp)
add_library(qcodeeditor STATIC ${QCE_SOURCES} ${qcodeeditor_SOURCE_DIR}/resources/qcodeeditor_resources.qrc)
target_include_directories(qcodeeditor PUBLIC ${qcodeeditor_SOURCE_DIR}/include ${qcodeeditor_SOURCE_DIR}/include/internal)
target_link_libraries(qcodeeditor PUBLIC Qt6::Widgets)

add_executable(wiktbrowse main.cpp)
target_link_libraries(wiktbrowse qcodeeditor nlohmann_json::nlohmann_json BZip2::BZip2)
