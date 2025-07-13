enable_testing()

find_package(GTest REQUIRED)

add_executable(tests
        test_template_parsing.cpp
        ../src1/factory/TagFactory.cpp
        ../src1/elements/TaggedContent.cpp
        ../src1/parser/Parser.cpp
        ../src1/elements/WikiVisitor.h
        ../src1/elements/TextElement.cpp
        ../src1/parser/ParserError.cpp
        ../src1/elements/WikiElement.cpp
        ../src1/elements/Template.cpp
        ../src1/elements/Tag.cpp
        test_header_parsing.cpp
        ../src1/elements/Header.cpp
        test_wikilink_parsing.cpp
        ../src1/elements/WikiLink.cpp
        test_external_link_parsing.cpp
        ../src1/elements/ExternalLink.cpp
        test_tag_parsing.cpp
        ../src1/elements/LeafElement.h
        ../src1/elements/ContainerElement.h
)

target_link_libraries(tests
        PRIVATE
        GTest::GTest
        GTest::Main
)

target_compile_definitions(tests PRIVATE UNIT_TEST)

include(GoogleTest)
gtest_discover_tests(tests)
