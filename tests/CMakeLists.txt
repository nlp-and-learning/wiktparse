enable_testing()

find_package(GTest REQUIRED)

# Wikitext parsing is tested in wikilib (tests/markup/test_wiktparse_cases.cpp)
add_executable(tests
        WikiDataTest.cpp
        WikiTermsTest.cpp
        LuaDataTest.cpp
        DefglosTest.cpp
)

target_link_libraries(tests
        PRIVATE
        wiktdump
        defglos
        GTest::GTest
        GTest::Main
)

target_compile_definitions(tests PRIVATE UNIT_TEST WIKTPARSE_RES_DIR="${PROJECT_SOURCE_DIR}/res")

include(GoogleTest)
gtest_discover_tests(tests)
