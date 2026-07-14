#include <string.h>

#include "nyx/scanner.h"
#include "test_harness.h"

static Token nextTok(void) { return scanToken(); }

void run_scanner_tests(void) {
    TEST_SUITE("scanner");

    initScanner("var x = 12.5;");
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_VAR);
    Token id = nextTok();
    TEST_CHECK_EQ_INT(id.type, TOKEN_IDENTIFIER);
    TEST_CHECK_EQ_INT(id.length, 1);
    TEST_CHECK(id.start[0] == 'x');
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_EQUAL);
    Token num = nextTok();
    TEST_CHECK_EQ_INT(num.type, TOKEN_NUMBER);
    TEST_CHECK_EQ_INT(num.length, 4); /* "12.5" */
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_SEMICOLON);
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_EOF);

    initScanner("\"hello\\nworld\"");
    Token str = nextTok();
    TEST_CHECK_EQ_INT(str.type, TOKEN_STRING);

    initScanner("// comment\n/* block\n comment */ fun");
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_FUN);

    initScanner("== != <= >= < > = ! + - * / %");
    TokenType expected[] = {
        TOKEN_EQUAL_EQUAL, TOKEN_BANG_EQUAL, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
        TOKEN_LESS, TOKEN_GREATER, TOKEN_EQUAL, TOKEN_BANG,
        TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        TEST_CHECK_EQ_INT(nextTok().type, expected[i]);
    }

    initScanner("and or true false nil if else while for return break continue");
    TokenType keywords[] = {
        TOKEN_AND, TOKEN_OR, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL, TOKEN_IF, TOKEN_ELSE,
        TOKEN_WHILE, TOKEN_FOR, TOKEN_RETURN, TOKEN_BREAK, TOKEN_CONTINUE,
    };
    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        TEST_CHECK_EQ_INT(nextTok().type, keywords[i]);
    }

    initScanner("[1, 2] {\"a\": 1}");
    TokenType brackets[] = {
        TOKEN_LEFT_BRACKET, TOKEN_NUMBER, TOKEN_COMMA, TOKEN_NUMBER, TOKEN_RIGHT_BRACKET,
        TOKEN_LEFT_BRACE, TOKEN_STRING, TOKEN_COLON, TOKEN_NUMBER, TOKEN_RIGHT_BRACE,
    };
    for (size_t i = 0; i < sizeof(brackets) / sizeof(brackets[0]); i++) {
        TEST_CHECK_EQ_INT(nextTok().type, brackets[i]);
    }

    initScanner("\"unterminated");
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_ERROR);

    initScanner("@");
    TEST_CHECK_EQ_INT(nextTok().type, TOKEN_ERROR);
}
