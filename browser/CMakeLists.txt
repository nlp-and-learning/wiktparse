# wiktbrowse: browser of wikt2jsonl output (.jsonl, or .jsonl.bz2 with its
# .idx). A separate project, so that wiktparse itself needs no Qt:
#   cmake -S browser -B browser/build -DCMAKE_BUILD_TYPE=Release && cmake --build browser/build
cmake_minimum_required(VERSION 3.24)
project(wiktbrowse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(nlohmann_json REQUIRED)
find_package(BZip2 REQUIRED)

# qcodeedit (LGPL-3.0): the editor widget with Kate syntax highlighting and
# folding. The sibling checkout ../../qcodeedit when present, otherwise fetched.
set(QCE_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(QCE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QCE_BUILD_KATE ON CACHE BOOL "" FORCE)
foreach(option QCE_BUILD_KATE_DOWNLOADER QCE_BUILD_ENCODING QCE_BUILD_GRAMMAR QCE_BUILD_SPELL
        QCE_BUILD_SPELL_DOWNLOADER QCE_BUILD_SPELLTUNE QCE_BUILD_DIFF)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
include(FetchContent)
set(QCODEEDIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../qcodeedit CACHE PATH "qcodeedit sources")
if(EXISTS ${QCODEEDIT_DIR}/CMakeLists.txt)
    FetchContent_Declare(qcodeedit SOURCE_DIR ${QCODEEDIT_DIR})
else()
    FetchContent_Declare(qcodeedit GIT_REPOSITORY https://gitlab.com/siplasplas/qcodeedit.git GIT_TAG master)
endif()
FetchContent_MakeAvailable(qcodeedit)

# qt-extra: the file dialog. The sibling checkout ../../qt-extra when present, otherwise fetched.
set(QT_EXTRA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../qt-extra CACHE PATH "qt-extra sources")
if(EXISTS ${QT_EXTRA_DIR}/CMakeLists.txt)
    FetchContent_Declare(qt-extra SOURCE_DIR ${QT_EXTRA_DIR})
else()
    FetchContent_Declare(qt-extra GIT_REPOSITORY https://github.com/siplasplas/qt-extra.git GIT_TAG master)
endif()
FetchContent_MakeAvailable(qt-extra)

add_executable(wiktbrowse main.cpp)
target_link_libraries(wiktbrowse Qt6::Widgets qcodeedit::qcodeedit qcodeedit::kate qt-extra nlohmann_json::nlohmann_json
        BZip2::BZip2)
