#include <string.h>

#include "nyx/common.h"
#include "nyx/scanner.h"

typedef struct {
    const char *start;
    const char *current;
    int line;
} Scanner;

static Scanner scanner;

void initScanner(const char *source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isAtEnd(void) { return *scanner.current == '\0'; }

static char advance(void) {
    scanner.current++;
    return scanner.current[-1];
}

static char peek(void) { return *scanner.current; }

static char peekNext(void) {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

static void skipWhitespaceAndComments(void) {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if (peekNext() == '*') {
                    advance();
                    advance();
                    int depth = 1;
                    while (depth > 0 && !isAtEnd()) {
                        if (peek() == '/' && peekNext() == '*') {
                            advance();
                            advance();
                            depth++;
                        } else if (peek() == '*' && peekNext() == '/') {
                            advance();
                            advance();
                            depth--;
                        } else {
                            if (peek() == '\n') scanner.line++;
                            advance();
                        }
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static bool isDigit(char c) { return c >= '0' && c <= '9'; }

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static TokenType checkKeyword(int start, int length, const char *rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, (size_t)length) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(void) {
    switch (scanner.start[0]) {
        case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
        case 'c': return checkKeyword(1, 7, "ontinue", TOKEN_CONTINUE);
        case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 't': return checkKeyword(1, 3, "rue", TOKEN_TRUE);
        case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token identifier(void) {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

static Token number(void) {
    while (isDigit(peek())) advance();
    if (peek() == '.' && isDigit(peekNext())) {
        advance();
        while (isDigit(peek())) advance();
    }
    return makeToken(TOKEN_NUMBER);
}

static Token string(void) {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        if (peek() == '\\' && peekNext() != '\0') advance();
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    advance();
    return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
    skipWhitespaceAndComments();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(': return makeToken(TOKEN_LEFT_PAREN);
        case ')': return makeToken(TOKEN_RIGHT_PAREN);
        case '{': return makeToken(TOKEN_LEFT_BRACE);
        case '}': return makeToken(TOKEN_RIGHT_BRACE);
        case '[': return makeToken(TOKEN_LEFT_BRACKET);
        case ']': return makeToken(TOKEN_RIGHT_BRACKET);
        case ';': return makeToken(TOKEN_SEMICOLON);
        case ':': return makeToken(TOKEN_COLON);
        case ',': return makeToken(TOKEN_COMMA);
        case '.': return makeToken(TOKEN_DOT);
        case '-': return makeToken(TOKEN_MINUS);
        case '+': return makeToken(TOKEN_PLUS);
        case '/': return makeToken(TOKEN_SLASH);
        case '*': return makeToken(TOKEN_STAR);
        case '%': return makeToken(TOKEN_PERCENT);
        case '!': return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=': return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
        case '<': return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>': return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return string();
    }

    return errorToken("Unexpected character.");
}

const char *tokenTypeName(TokenType type) {
    static const char *names[] = {
        "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACE", "RIGHT_BRACE",
        "LEFT_BRACKET", "RIGHT_BRACKET", "COMMA", "DOT", "MINUS", "PLUS",
        "SEMICOLON", "COLON", "SLASH", "STAR", "PERCENT",
        "BANG", "BANG_EQUAL", "EQUAL", "EQUAL_EQUAL",
        "GREATER", "GREATER_EQUAL", "LESS", "LESS_EQUAL",
        "IDENTIFIER", "STRING", "NUMBER",
        "AND", "ELSE", "FALSE", "FOR", "FUN", "IF", "NIL", "OR", "RETURN",
        "TRUE", "VAR", "WHILE", "BREAK", "CONTINUE",
        "ERROR", "EOF",
    };
    return names[type];
}
