#include "katvan_parsing.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <QString>

#include <iostream>

using namespace katvan::parsing;

namespace katvan::parsing {
    void PrintTo(const Token& token, std::ostream* os) {
        *os << "Token(" << static_cast<int>(token.type)
            << ", " << token.startPos << ", " << token.length
            << ", \"" << token.text.toUtf8().data() << "\")";
    }

    void PrintTo(const HiglightingMarker& marker, std::ostream* os) {
        *os << "HiglightingMarker(" << static_cast<int>(marker.kind)
            << ", " << marker.startPos << ", " << marker.length << ")";
    }
}

struct TokenMatcher {
    TokenType type;
    QString text;
};

bool operator==(const Token& t, const TokenMatcher& m) {
    return m.type == t.type && m.text == t.text;
}

void PrintTo(const TokenMatcher& m, std::ostream* os) {
    *os << "TokenMatcher(" << static_cast<int>(m.type)
        << ", \"" << m.text.toStdString() << "\")";
}

static std::vector<Token> tokenizeString(const QString& str)
{
    std::vector<Token> result;

    Tokenizer tok(str);
    while (!tok.atEnd()) {
        result.push_back(tok.nextToken());
    }
    return result;
}

TEST(TokenizerTests, TestEmpty) {
    Tokenizer tok(QStringLiteral(""));

    ASSERT_FALSE(tok.atEnd());
    ASSERT_EQ(tok.nextToken(), (TokenMatcher{ TokenType::BEGIN }));
    ASSERT_TRUE(tok.atEnd());
    ASSERT_EQ(tok.nextToken(), (TokenMatcher{ TokenType::TEXT_END }));
    ASSERT_TRUE(tok.atEnd());
}

TEST(TokenizerTests, BasicSanity) {
    auto tokens = tokenizeString(QStringLiteral("a very basic test, with 10 words (or so!)"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("a") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("very") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("b") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("asic") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("test") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral(",") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("with") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("10") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("words") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("(") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("o") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("r") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("so") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("!") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral(")") }
    }));
}

TEST(TokenizerTests, WhiteSpace) {
    auto tokens = tokenizeString(QStringLiteral(" A   B\tC  \t \nD\r\n\nE F"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("A") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral("   ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("B") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral("\t") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("C") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral("  \t ") },
        TokenMatcher{ TokenType::LINE_END,     QStringLiteral("\n") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("D") },
        TokenMatcher{ TokenType::LINE_END,     QStringLiteral("\r\n") },
        TokenMatcher{ TokenType::LINE_END,     QStringLiteral("\n") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("E") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("F") }
    }));
}

TEST(TokenizerTests, Escapes) {
    std::vector<Token> tokens;

    tokens = tokenizeString(QStringLiteral(R"(A \$ $\"'\'abc)"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("A") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\$") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("$") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\"") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("'") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\'") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("abc") }
    }));

    tokens = tokenizeString(QStringLiteral(R"(\\\\\\\\\)"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\\") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\\") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\\") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\\") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("\\") }
    }));

    tokens = tokenizeString(QStringLiteral(R"(\u{12e} \u{1f600} \\u{123})"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\u{12e}") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\u{1f600}") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::ESCAPE,       QStringLiteral("\\\\") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("u") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("{") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("123") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("}") }
    }));
}

TEST(TokenizerTests, Niqqud) {
    auto tokens = tokenizeString(QStringLiteral("שָׁלוֹם עוֹלָם 12"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("שָׁלוֹם") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("עוֹלָם") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("12") }
    }));
}

TEST(TokenizerTests, NotIdentifier) {
    auto tokens = tokenizeString(QStringLiteral("a _small_ thing"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("a") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("_") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("small") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("_") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("thing") }
    }));
}


TEST(TokenizerTests, Identifier) {
    auto tokens = tokenizeString(QStringLiteral("#let a_b3z = [$a$]"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("#") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("let") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("a_b3z") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("=") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("[") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("$") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("a") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("$") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("]") }
    }));
}

TEST(TokenizerTests, MirroredSymbols) {
    auto tokens = tokenizeString(QStringLiteral("לפני [באמצע] אחרי"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("לפני") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("[") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("באמצע") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("]") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("אחרי") }
    }));
}

TEST(TokenizerTests, FullCodeNumber) {
    auto tokens = tokenizeString(QStringLiteral("A -12.4e-15em + 4e2B"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("A") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("-12.4e-15") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("em") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("+") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("4e2") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("B") }
    }));
}

TEST(TokenizerTests, HexCodeNumber) {
    auto tokens = tokenizeString(QStringLiteral("x10CAFE.b DEADBEEF xavier"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("x10CAFE.b") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("DEADBEEF") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("xa") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("vier") }
    }));
}

TEST(TokenizerTests, CodeNumberBacktracking) {
    auto tokens = tokenizeString(QStringLiteral("-b 12e-"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("-") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("b") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::CODE_NUMBER,  QStringLiteral("12") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("e") },
        TokenMatcher{ TokenType::SYMBOL,       QStringLiteral("-") }
    }));
}

TEST(TokenizerTests, NonLatinNumerals) {
    auto tokens = tokenizeString(QStringLiteral("هناك ١٢ قطط"));
    EXPECT_THAT(tokens, ::testing::ElementsAreArray({
        TokenMatcher{ TokenType::BEGIN },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("هناك") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("١٢") },
        TokenMatcher{ TokenType::WHITESPACE,   QStringLiteral(" ") },
        TokenMatcher{ TokenType::WORD,         QStringLiteral("قطط") }
    }));
}

static QList<HiglightingMarker> highlightText(QStringView text)
{
    HighlightingListener listener;
    Parser parser(text, listener);
    parser.parse();
    return listener.markers();
}

TEST(HiglightingParserTests, LineComment) {
    auto markers = highlightText(QStringLiteral("a // comment line\nb"));
    EXPECT_THAT(markers, ::testing::ElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT, 2, 16 }
    ));
}

TEST(HiglightingParserTests, BlockComment) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("a /* comment\ncomment\ncomment*/ b"));
    EXPECT_THAT(markers, ::testing::ElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT, 2, 28 }
    ));

    markers = highlightText(QStringLiteral("/* aaa\naaa // aaaaaaa */\naaa*/ aaaa"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT, 11, 14 },
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT, 0, 30 }
    ));
}

TEST(HiglightingParserTests, StringLiteral) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("\"not a literal\" $ \"yesliteral\" + 1$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER, 16,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL, 18, 12 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,  31,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER, 34,  1 }

    ));

    markers = highlightText(QStringLiteral("$ \"A /* $ \" */ $"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  0, 1 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  2, 9 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,  12, 1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,  13, 1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER, 15, 1 }
    ));

    markers = highlightText(QStringLiteral("\"not a literal\" #foo(\"yesliteral\")"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,  16,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL, 21, 12 }
    ));
}

TEST(HiglightingParserTests, Escapes) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("_\\$ \\_ foo _ \\ More: \"\\u{1f600}\""));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::EMPHASIS,  0, 12 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,    1,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,    4,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,   22,  9 }
    ));

    markers = highlightText(QStringLiteral("$ \\u{12} + \"a\\nb\" $"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  0, 1 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,          2, 6 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,   9, 1 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL, 11, 6 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,         13, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER, 18, 1 }
    ));
}

TEST(HiglightingParserTests, Heading) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("=== this is a heading\nthis is not.\n \t= but this is"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::HEADING, 0, 22 },
        HiglightingMarker{ HiglightingMarker::Kind::HEADING, 34, 16 }
    ));

    markers = highlightText(QStringLiteral("a == not header\n=not header too"));
    EXPECT_THAT(markers, ::testing::IsEmpty());
}

TEST(HiglightingParserTests, Emphasis) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("a *bold* _underline_ and _*nested*_"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::STRONG_EMPHASIS, 2, 6 },
        HiglightingMarker{ HiglightingMarker::Kind::EMPHASIS, 9, 11 },
        HiglightingMarker{ HiglightingMarker::Kind::EMPHASIS, 25, 10 },
        HiglightingMarker{ HiglightingMarker::Kind::STRONG_EMPHASIS, 26, 8 }
    ));

    markers = highlightText(QStringLiteral("== for some reason, _emphasis\nextends_ headers"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::HEADING, 0, 46 },
        HiglightingMarker{ HiglightingMarker::Kind::EMPHASIS, 20, 18 }
    ));

    markers = highlightText(QStringLiteral("*bold broken by paragraph break\n  \n*"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::STRONG_EMPHASIS,  0, 35 },
        HiglightingMarker{ HiglightingMarker::Kind::STRONG_EMPHASIS, 35,  1 }
    ));
}

TEST(HiglightingParserTests, RawContent) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("`` `some $raw$ with _emph_` `raw with\nnewline`"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::RAW, 0, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::RAW, 3, 24 },
        HiglightingMarker{ HiglightingMarker::Kind::RAW, 28, 18 }
    ));

    markers = highlightText(QStringLiteral("```some $raw$ with _emph_` ``` ```raw block with\nnewline```"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::RAW, 0, 30 },
        HiglightingMarker{ HiglightingMarker::Kind::RAW, 31, 28 }
    ));
}

TEST(HiglightingParserTests, ReferenceAndLabel) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("@ref123 foo <a_label> <not a label> //<also_not_label"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::REFERENCE, 0,   7 },
        HiglightingMarker{ HiglightingMarker::Kind::LABEL,     12,  9 },
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT,   36, 17 }
    ));

    markers = highlightText(QStringLiteral("<label_with_trailing_>\n@a_reference_with_trailing__"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::LABEL,     0,  22 },
        HiglightingMarker{ HiglightingMarker::Kind::REFERENCE, 23, 28 }
    ));

    markers = highlightText(QStringLiteral("== The nature of @label\n_this is the <label>_"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::HEADING,   0 , 24 },
        HiglightingMarker{ HiglightingMarker::Kind::REFERENCE, 17,  6 },
        HiglightingMarker{ HiglightingMarker::Kind::EMPHASIS,  24, 21 },
        HiglightingMarker{ HiglightingMarker::Kind::LABEL,     37,  7 }
    ));
}

TEST(HiglightingParserTests, Lists) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("- - this\n- this\n\t- that"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY,  0, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY,  8, 3 },
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY, 15, 4 }
    ));

    markers = highlightText(QStringLiteral("+ - this\n+this\n\t+ that"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY,  0, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY, 14, 4 }
    ));

    markers = highlightText(QStringLiteral("/ This: That\n/Not This: Not that\n/Neither This"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::LIST_ENTRY, 0, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::TERM,       2, 4 }
    ));
}

/*
 * Test cases taken from Typst documentation
 */

TEST(HiglightingParserTests, MathExpressions) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral("$x^2$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,    2,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   4,  1 }
    ));

    markers = highlightText(QStringLiteral("$x &= 2 \\ &= 3$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,    3,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,    4,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,    8,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,   10,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,   11,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  14,  1 }
    ));

    markers = highlightText(QStringLiteral("$#x$, $pi$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,    1,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   3,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   6,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,    7,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   9,  1 }
    ));

    markers = highlightText(QStringLiteral("$arrow.r.long$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,    1,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,    7,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,    9,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  13,  1 }
    ));

    markers = highlightText(QStringLiteral("$floor(x)$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,    1,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   9,  1 }
    ));

    markers = highlightText(QStringLiteral("$#rect(width: 1cm) + 1$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,    1,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  14,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_OPERATOR,   19,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  22,  1 }
    ));

    markers = highlightText(QStringLiteral("$/* comment */$"));
    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,   0,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::COMMENT,          1, 13 },
        HiglightingMarker{ HiglightingMarker::Kind::MATH_DELIMITER,  14,  1 }
    ));
}

TEST(HiglightingParserTests, SetRules) {
    QList<HiglightingMarker> markers;

    markers = highlightText(QStringLiteral(
        "#set heading(numbering: \"I.\")\n"
        "#set text(\n"
        "  font: \"New Computer Modern\"\n"
        ")\n\n"
        "= Introduction"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          0,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,    5,  7 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  24,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         30,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   35,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  49, 21 },
        HiglightingMarker{ HiglightingMarker::Kind::HEADING,         73, 15 }
    ));

    markers = highlightText(QStringLiteral(
        "#let task(body, critical: false) = {\n"
        "  set text(red) if critical\n"
        "  [- #body]\n"
        "}\n\n"
        "#task(critical: true)[Food today?]\n"
        "#task(critical: false)[Work deadline]"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          0, 4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,    5, 4 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         26, 5 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         39, 3 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   43, 4 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         53, 2 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   70, 5 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   80, 5 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         96, 4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,  115, 5 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,        131, 5 }
    ));
}

TEST(HiglightingParserTests, ShowRules) {
    auto markers = highlightText(QStringLiteral(
        "#show heading: it => [\n"
        "  #set align(center)\n"
        "  #set text(font: \"Inria Serif\")\n"
        "  \\~ #emph(it.body)\n"
        "      #counter(heading).display() \\~\n"
        "]"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          0,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         25,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   30,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         46,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   51,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  62, 13 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,          79,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   82,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,  103,  8 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,  121,  7 },
        HiglightingMarker{ HiglightingMarker::Kind::ESCAPE,         131,  2 }
    ));
}

TEST(HiglightingParserTests, CodeExpressions) {
    auto markers = highlightText(QStringLiteral(
        "#emph[Hello] \\\n"
        "#emoji.face \\\n"
        "#\"hello\".len().a\n"
        "#(40em.abs.inches(), 12%)\n"
        "#40em.abs.inches()"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,    0,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   15,  6 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   22,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  29,  8 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   38,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   44,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  48,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   57,  6 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  67,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  72,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   78,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::FUNCTION_NAME,   82,  6 }
    ));
}

TEST(HiglightingParserTests, Blocks) {
    auto markers = highlightText(QStringLiteral(
        "#{\n"
        "let a = [from]\n"
        "let b = [*world*]\n"
        "[hello ]\n"
        "a + [ the ] + b\n"
        "}"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          3,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         18,  3 },
        HiglightingMarker{ HiglightingMarker::Kind::STRONG_EMPHASIS, 27,  7 }
    ));
}

TEST(HiglightingParserTests, Loops) {
    auto markers = highlightText(QStringLiteral(
        "#for c in \"ABC\" [\n"
        "  #c is a letter.\n"
        "]\n\n"
        "#let n = 2\n"
        "#while n < 10 {\n"
        "  n = (n * 2) - 1\n"
        "}"));

    EXPECT_THAT(markers, ::testing::UnorderedElementsAre(
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          0,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,          7,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::STRING_LITERAL,  10,  5 },
        HiglightingMarker{ HiglightingMarker::Kind::VARIABLE_NAME,   20,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         39,  4 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  48,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::KEYWORD,         50,  6 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  61,  2 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  77,  1 },
        HiglightingMarker{ HiglightingMarker::Kind::NUMBER_LITERAL,  82,  1 }
    ));
}
