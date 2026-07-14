#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nyx/debug.h"
#include "nyx/vm.h"

static char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "nyx: could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *)malloc((size_t)fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "nyx: not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), (size_t)fileSize, file);
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

static void repl(void) {
    char line[4096];
    printf("nyx %s -- Ctrl+D (or Ctrl+Z on Windows) to exit\n", "0.1.0");
    for (;;) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        interpret(line);
    }
}

static void runFile(const char *path) {
    char *source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void disassembleRecursive(Chunk *chunk, const char *name) {
    disassembleChunk(chunk, name);
    for (int i = 0; i < chunk->constants.count; i++) {
        Value v = chunk->constants.values[i];
        if (IS_OBJ(v) && OBJ_TYPE(v) == OBJ_FUNCTION) {
            ObjFunction *nested = AS_FUNCTION(v);
            char label[256];
            snprintf(label, sizeof(label), "fn %s",
                     nested->name == NULL ? "<script>" : nested->name->chars);
            printf("\n");
            disassembleRecursive(&nested->chunk, label);
        }
    }
}

static void disasmFile(const char *path) {
    char *source = readFile(path);
    ObjFunction *function = NULL;
    InterpretResult result = interpretChunkForDisasm(source, &function);
    free(source);

    if (result == INTERPRET_COMPILE_ERROR || function == NULL) {
        exit(65);
    }
    disassembleRecursive(&function->chunk, path);
}

int main(int argc, char *argv[]) {
    initVM();

    if (argc == 1) {
        repl();
    } else if (argc == 3 && strcmp(argv[1], "--disasm") == 0) {
        disasmFile(argv[2]);
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        fprintf(stderr, "Usage: nyx [path]\n       nyx --disasm <path>\n");
        freeVM();
        exit(64);
    }

    freeVM();
    return 0;
}
