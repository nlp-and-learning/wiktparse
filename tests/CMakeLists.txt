enable_testing()

find_package(GTest REQUIRED)

# Wikitext parsing is tested in wikilib (tests/markup/test_wiktparse_cases.cpp)
add_executable(tests
        WikiDataTest.cpp
        WikiTermsTest.cpp
        LuaDataTest.cpp
)

target_link_libraries(tests
        PRIVATE
        wiktdump
        GTest::GTest
        GTest::Main
)

target_compile_definitions(tests PRIVATE UNIT_TEST)

include(GoogleTest)
gtest_discover_tests(tests)
