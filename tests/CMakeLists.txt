enable_testing()

find_package(GTest REQUIRED)

add_executable(tests TemplateTest.cpp
        CommentTest.cpp
        CompositeTextTest.cpp
        TagTest.cpp
        HeaderTest.cpp
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
        test_template_parsing.cpp
        ../src1/factory/TagFactory.cpp
        ../src1/elements/TaggedContent.cpp
        ../src1/parser/Parser.cpp
        ../src1/elements/WikiVisitor.h
        ../src1/elements/TextFragment.cpp
        ../src1/parser/ParserError.cpp
        ../src1/elements/WikiElement.cpp
        ../src1/elements/Template.cpp
        ../src1/elements/Tag.cpp
)

target_link_libraries(tests
        PRIVATE
        GTest::GTest
        GTest::Main
)

target_compile_definitions(tests PRIVATE UNIT_TEST)

include(GoogleTest)
gtest_discover_tests(tests)
