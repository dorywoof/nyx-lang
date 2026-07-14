#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nyx/compiler.h"
#include "nyx/memory.h"
#include "nyx/object.h"
#include "nyx/scanner.h"
#include "nyx/vm.h"

#define UINT8_COUNT (UINT8_MAX + 1)
#define MAX_BREAKS 64

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
} Local;

typedef struct {
    uint8_t index;
    bool isLocal;
} UpvalueRef;

typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
} FunctionType;

typedef struct LoopState {
    struct LoopState *enclosing;
    int continueTarget;
    int breakJumps[MAX_BREAKS];
    int breakJumpCount;
} LoopState;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction *function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    UpvalueRef upvalues[UINT8_COUNT];
    int scopeDepth;

    LoopState *loop;
} Compiler;

static Parser parser;
static Compiler *current = NULL;

static Chunk *currentChunk(void) { return &current->function->chunk; }

static void errorAt(Token *token, const char *message) {
    if (parser.panicMode) return;
    parser.panicMode = true;

    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void errorAtCurrent(const char *message) { errorAt(&parser.current, message); }
static void error(const char *message) { errorAt(&parser.previous, message); }

static void advanceParser(void) {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char *message) {
    if (parser.current.type == type) {
        advanceParser();
        return;
    }
    errorAtCurrent(message);
}

static bool check(TokenType type) { return parser.current.type == type; }

static bool matchToken(TokenType type) {
    if (!check(type)) return false;
    advanceParser();
    return true;
}

static void emitByte(uint8_t byte) { writeChunk(currentChunk(), byte, parser.previous.line); }
static void emitBytes(uint8_t b1, uint8_t b2) {
    emitByte(b1);
    emitByte(b2);
}

static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

static void patchJump(int offset) {
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

static void emitReturn(void) {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }
    return (uint8_t)constant;
}

static void emitConstant(Value value) { emitBytes(OP_CONSTANT, makeConstant(value)); }

static void initCompiler(Compiler *compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->loop = NULL;
    compiler->function = newFunction();
    current = compiler;

    if (type != TYPE_SCRIPT) {
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    Local *local = &current->locals[current->localCount++];
    local->depth = 0;
    local->isCaptured = false;
    local->name.start = "";
    local->name.length = 0;
}

static ObjFunction *endCompiler(void) {
    emitReturn();
    ObjFunction *function = current->function;
    current = current->enclosing;
    return function;
}

static void beginScope(void) { current->scopeDepth++; }

static void endScope(void) {
    current->scopeDepth--;
    while (current->localCount > 0 &&
           current->locals[current->localCount - 1].depth > current->scopeDepth) {
        if (current->locals[current->localCount - 1].isCaptured) {
            emitByte(OP_CLOSE_UPVALUE);
        } else {
            emitByte(OP_POP);
        }
        current->localCount--;
    }
}

static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule *getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint8_t identifierConstant(Token *name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, (size_t)a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }
    return -1;
}

static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        UpvalueRef *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) return i;
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in one function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler *compiler, Token *name) {
    if (compiler->enclosing == NULL) return -1;

    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        compiler->enclosing->locals[local].isCaptured = true;
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

static void addLocal(Token name) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in one function.");
        return;
    }
    Local *local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}

static void declareVariable(void) {
    if (current->scopeDepth == 0) return;

    Token *name = &parser.previous;
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local *local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            error("A variable with this name already exists in this scope.");
        }
    }
    addLocal(*name);
}

static uint8_t parseVariable(const char *errorMessage) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable();
    if (current->scopeDepth > 0) return 0;

    return identifierConstant(&parser.previous);
}

static void markInitialized(void) {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }
    emitBytes(OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList(void) {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) error("Can't have more than 255 arguments.");
            argCount++;
        } while (matchToken(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

static void and_(bool canAssign) {
    (void)canAssign;
    int endJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_AND);
    patchJump(endJump);
}

static void or_(bool canAssign) {
    (void)canAssign;
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

static void binary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    ParseRule *rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL: emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
        case TOKEN_GREATER: emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS: emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL: emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        case TOKEN_PERCENT: emitByte(OP_MODULO); break;
        default: return;
    }
}

static void call(bool canAssign) {
    (void)canAssign;
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

static void index_(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    if (canAssign && matchToken(TOKEN_EQUAL)) {
        expression();
        emitByte(OP_SET_INDEX);
    } else {
        emitByte(OP_GET_INDEX);
    }
}

static void literal(bool canAssign) {
    (void)canAssign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return;
    }
}

static void grouping(bool canAssign) {
    (void)canAssign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign) {
    (void)canAssign;
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static ObjString *unescapeString(const char *raw, int rawLength) {
    char *buf = (char *)malloc((size_t)rawLength + 1);
    int len = 0;
    for (int i = 0; i < rawLength; i++) {
        if (raw[i] == '\\' && i + 1 < rawLength) {
            char next = raw[i + 1];
            switch (next) {
                case 'n': buf[len++] = '\n'; i++; break;
                case 't': buf[len++] = '\t'; i++; break;
                case 'r': buf[len++] = '\r'; i++; break;
                case '\\': buf[len++] = '\\'; i++; break;
                case '"': buf[len++] = '"'; i++; break;
                case '0': buf[len++] = '\0'; i++; break;
                default: buf[len++] = raw[i]; break;
            }
        } else {
            buf[len++] = raw[i];
        }
    }
    ObjString *result = copyString(buf, len);
    free(buf);
    return result;
}

static void string(bool canAssign) {
    (void)canAssign;
    ObjString *s = unescapeString(parser.previous.start + 1, parser.previous.length - 2);
    emitConstant(OBJ_VAL(s));
}

static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && matchToken(TOKEN_EQUAL)) {
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

static void variable(bool canAssign) { namedVariable(parser.previous, canAssign); }

static void arrayLiteral(bool canAssign) {
    (void)canAssign;
    uint8_t count = 0;
    if (!check(TOKEN_RIGHT_BRACKET)) {
        do {
            expression();
            if (count == 255) error("Can't have more than 255 elements in an array literal.");
            count++;
        } while (matchToken(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACKET, "Expect ']' after array elements.");
    emitBytes(OP_ARRAY, count);
}

static void mapLiteral(bool canAssign) {
    (void)canAssign;
    uint8_t count = 0;
    if (!check(TOKEN_RIGHT_BRACE)) {
        do {
            expression();
            consume(TOKEN_COLON, "Expect ':' after map key.");
            expression();
            if (count == 255) error("Can't have more than 255 pairs in a map literal.");
            count++;
        } while (matchToken(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after map entries.");
    emitBytes(OP_MAP, count);
}

static void unary(bool canAssign) {
    (void)canAssign;
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {mapLiteral, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACKET]  = {arrayLiteral, index_, PREC_CALL},
    [TOKEN_RIGHT_BRACKET] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]         = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT]           = {NULL, NULL, PREC_NONE},
    [TOKEN_MINUS]         = {unary, binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON]         = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL, binary, PREC_FACTOR},
    [TOKEN_PERCENT]       = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable, NULL, PREC_NONE},
    [TOKEN_STRING]        = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER]        = {number, NULL, PREC_NONE},
    [TOKEN_AND]           = {NULL, and_, PREC_AND},
    [TOKEN_ELSE]          = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE]         = {literal, NULL, PREC_NONE},
    [TOKEN_FOR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN]           = {NULL, NULL, PREC_NONE},
    [TOKEN_IF]            = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL]           = {literal, NULL, PREC_NONE},
    [TOKEN_OR]            = {NULL, or_, PREC_OR},
    [TOKEN_RETURN]        = {NULL, NULL, PREC_NONE},
    [TOKEN_TRUE]          = {literal, NULL, PREC_NONE},
    [TOKEN_VAR]           = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE]         = {NULL, NULL, PREC_NONE},
    [TOKEN_BREAK]         = {NULL, NULL, PREC_NONE},
    [TOKEN_CONTINUE]      = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]         = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF]           = {NULL, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
    advanceParser();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advanceParser();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    if (canAssign && matchToken(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

static ParseRule *getRule(TokenType type) { return &rules[type]; }

static void expression(void) { parsePrecedence(PREC_ASSIGNMENT); }

static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                errorAtCurrent("Can't have more than 255 parameters.");
            }
            uint8_t paramConstant = parseVariable("Expect parameter name.");
            defineVariable(paramConstant);
        } while (matchToken(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    ObjFunction *fn = endCompiler();
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(fn)));

    for (int i = 0; i < fn->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}

static void funDeclaration(void) {
    uint8_t global = parseVariable("Expect function name.");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
}

static void varDeclaration(void) {
    uint8_t global = parseVariable("Expect variable name.");

    if (matchToken(TOKEN_EQUAL)) {
        expression();
    } else {
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    defineVariable(global);
}

static void expressionStatement(void) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emitByte(OP_POP);
}

static void ifStatement(void) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    int elseJump = emitJump(OP_JUMP);
    patchJump(thenJump);
    emitByte(OP_POP);

    if (matchToken(TOKEN_ELSE)) statement();
    patchJump(elseJump);
}

static void beginLoop(LoopState *loop) {
    loop->enclosing = current->loop;
    loop->breakJumpCount = 0;
    current->loop = loop;
}

static void endLoop(void) {
    LoopState *loop = current->loop;
    for (int i = 0; i < loop->breakJumpCount; i++) {
        patchJump(loop->breakJumps[i]);
    }
    current->loop = loop->enclosing;
}

static void whileStatement(void) {
    LoopState loop;
    int loopStart = currentChunk()->count;
    beginLoop(&loop);
    loop.continueTarget = loopStart;

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
    endLoop();
}

static void forStatement(void) {
    beginScope();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if (matchToken(TOKEN_SEMICOLON)) {
    } else if (matchToken(TOKEN_VAR)) {
        varDeclaration();
    } else {
        expressionStatement();
    }

    LoopState loop;
    int loopStart = currentChunk()->count;
    beginLoop(&loop);
    loop.continueTarget = loopStart;

    int exitJump = -1;
    if (!matchToken(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    if (!matchToken(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        loop.continueTarget = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);

    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endLoop();
    endScope();
}

static void returnStatement(void) {
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }

    if (matchToken(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

static void breakStatement(void) {
    if (current->loop == NULL) {
        error("Can't use 'break' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
    LoopState *loop = current->loop;
    if (loop->breakJumpCount == MAX_BREAKS) {
        error("Too many 'break' statements in one loop.");
        return;
    }
    loop->breakJumps[loop->breakJumpCount++] = emitJump(OP_JUMP);
}

static void continueStatement(void) {
    if (current->loop == NULL) {
        error("Can't use 'continue' outside of a loop.");
        return;
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
    emitLoop(current->loop->continueTarget);
}

static void synchronize(void) {
    parser.panicMode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_RETURN:
                return;
            default:;
        }
        advanceParser();
    }
}

static void statement(void) {
    if (matchToken(TOKEN_IF)) {
        ifStatement();
    } else if (matchToken(TOKEN_WHILE)) {
        whileStatement();
    } else if (matchToken(TOKEN_FOR)) {
        forStatement();
    } else if (matchToken(TOKEN_RETURN)) {
        returnStatement();
    } else if (matchToken(TOKEN_BREAK)) {
        breakStatement();
    } else if (matchToken(TOKEN_CONTINUE)) {
        continueStatement();
    } else if (matchToken(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else {
        expressionStatement();
    }
}

static void declaration(void) {
    if (matchToken(TOKEN_FUN)) {
        funDeclaration();
    } else if (matchToken(TOKEN_VAR)) {
        varDeclaration();
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

ObjFunction *compile(const char *source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advanceParser();
    while (!matchToken(TOKEN_EOF)) {
        declaration();
    }

    ObjFunction *function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots(void) {
    Compiler *compiler = current;
    while (compiler != NULL) {
        markObject((Obj *)compiler->function);
        compiler = compiler->enclosing;
    }
}
