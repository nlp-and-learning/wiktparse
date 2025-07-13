// tests/test_preprocessing.cpp
#include <gtest/gtest.h>  // Google Test
#include "../src1/preprocessor/Preprocessor.h"  // For preprocess method
#include "../src1/elements/TextElement.h"  // TextFragment class

namespace preprocessor {
    // Fixture for preprocessing tests
    class PreprocessingTest : public ::testing::Test {
    protected:
        // No specific setup needed
    };

    TEST_F(PreprocessingTest, PreprocessWithComment) {
        // Input: abc<!-- comment -->def
        std::string input = "abc<!-- comment -->def";

        // Call preprocess
        auto fragments = Preprocessor::preprocess(input);

        // Expect 2 active fragments: "abc", "def"
        ASSERT_EQ(fragments.size(), 2);
        EXPECT_EQ(fragments[0]->get_text(), "abc");
        EXPECT_TRUE(fragments[0]->is_active());
        EXPECT_EQ(fragments[1]->get_text(), "def");
        EXPECT_TRUE(fragments[1]->is_active());
    }

    TEST_F(PreprocessingTest, PreprocessWithNowiki) {
        // Input: abc<nowiki>[[link]]</nowiki>def
        std::string input = "abc<nowiki>[[link]]</nowiki>def";

        // Call preprocess
        auto fragments = Preprocessor::preprocess(input);

        // Expect 3 fragments: "abc" active, "[[link]]" inactive, "def" active
        ASSERT_EQ(fragments.size(), 3);
        EXPECT_EQ(fragments[0]->get_text(), "abc");
        EXPECT_TRUE(fragments[0]->is_active());
        EXPECT_EQ(fragments[1]->get_text(), "[[link]]");
        EXPECT_FALSE(fragments[1]->is_active());
        EXPECT_EQ(fragments[2]->get_text(), "def");
        EXPECT_TRUE(fragments[2]->is_active());
    }

    TEST(CompositeTest, CommentBreakA) {
        std::string input = "a\n<!--comment -->\nb";
        auto fragments = Preprocessor::preprocess(input);
        ASSERT_EQ(fragments.size(), 2);
        EXPECT_EQ(fragments[0]->get_text(), "a\n");
        EXPECT_TRUE(fragments[0]->is_active());
        EXPECT_EQ(fragments[1]->get_text(), "b");
        EXPECT_TRUE(fragments[1]->is_active());
    }

}