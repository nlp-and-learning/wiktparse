enable_testing()

find_package(GTest REQUIRED)

add_executable(gtests test_templates.cpp
        CommentTest.cpp
        CompositeTextTest.cpp
        TagTest.cpp
        HeaderTest.cpp
        ../src/wikitext/old_TemplateParser.cpp
        ../src/wikitext/old_Template.cpp
        ../src/wikitext/old_text.cpp
        ../src/xml/Comments.cpp
        ../src/wikitext/CompositeText.h
        ../src/wikitext/TextParser.h
        ../src/tags/Tag.h
        ../src/tags/TagParser.h
        ../src/tags/Parser.h
        ../src/tags/TagFactory.h
)

target_link_libraries(gtests
        PRIVATE
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(gtests)
