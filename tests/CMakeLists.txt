enable_testing()

find_package(GTest REQUIRED)

add_executable(test_templates test_templates.cpp
        ../src/wikitext/old_TemplateParser.cpp
        ../src/wikitext/old_Template.cpp
        ../src/wikitext/old_text.cpp
        ../src/xml/Comments.cpp
        CommentsTest.cpp
        ../src/wikitext/CompositeText.h
        CompositeTextTest.cpp
        ../src/wikitext/TextParser.h
)
target_link_libraries(test_templates
        PRIVATE
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(test_templates)
