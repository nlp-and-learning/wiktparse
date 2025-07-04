enable_testing()

find_package(GTest REQUIRED)

add_executable(test_templates test_templates.cpp
        ../src/wikitext/TemplateParser.cpp
        ../src/wikitext/Template.cpp
        ../src/wikitext/text.cpp
)
target_link_libraries(test_templates
        PRIVATE
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(test_templates)
