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
        ../src/wikitext/CompositeText.h
        ../src/wikitext/TextParser.h
        ../src/markup/tags/Tag.h
        ../src/markup/tags/TagParser.h
        ../src/markup/BaseParser.h
        ../src/markup/tags/TagFactory.h
        ../src/markup/Comments.h
        ../src/markup/WikiText.h
        ../src/markup/wikinodes/TemplateParser.cpp
        ../src/markup/MarkupParser.cpp
        NowikiTest.cpp
        WikiLinkTest.cpp
)

target_link_libraries(gtests
        PRIVATE
        GTest::GTest
        GTest::Main
)

include(GoogleTest)
gtest_discover_tests(gtests)
