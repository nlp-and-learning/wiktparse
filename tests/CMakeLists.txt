enable_testing()

find_package(GTest REQUIRED)

add_executable(test_templates test_templates.cpp
        ../src/wikitext/old_TemplateParser.cpp
        ../src/wikitext/old_Template.cpp
        ../src/wikitext/old_text.cpp
        ../src/xml/Comments.cpp
        CommentTest.cpp
        ../src/wikitext/CompositeText.h
        CompositeTextTest.cpp
        ../src/wikitext/TextParser.h
        ../src/wikitext/Tag.h
        ../src/wikitext/TagParser.h
        TagTest.cpp
        HeaderTest.cpp
)
target_link_libraries(test_templates
        PRIVATE
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(test_templates)
