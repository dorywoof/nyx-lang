#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#define nyx_dup _dup
#define nyx_dup2 _dup2
#define nyx_close _close
#define nyx_fileno _fileno
#else
#include <unistd.h>
#define nyx_dup dup
#define nyx_dup2 dup2
#define nyx_close close
#define nyx_fileno fileno
#endif

#include "nyx/vm.h"
#include "test_harness.h"

/* Redirects the process's stdout fd to an unnamed temp file for the
 * duration of `interpret(src)`, then restores it and returns whatever was
 * printed. This is a genuine end-to-end test: it exercises the scanner,
 * compiler and VM together exactly the way running a .nyx script does. */
static char *runCapture(const char *src) {
    fflush(stdout);
    int savedFd = nyx_dup(nyx_fileno(stdout));
    FILE *tmp = tmpfile();
    nyx_dup2(nyx_fileno(tmp), nyx_fileno(stdout));

    interpret(src);

    fflush(stdout);
    nyx_dup2(savedFd, nyx_fileno(stdout));
    nyx_close(savedFd);

    fseek(tmp, 0, SEEK_END);
    long size = ftell(tmp);
    rewind(tmp);
    char *buf = (char *)malloc((size_t)size + 1);
    size_t n = fread(buf, 1, (size_t)size, tmp);
    buf[n] = '\0';
    fclose(tmp);
    return buf;
}

static void expectOutput(const char *name, const char *src, const char *expected) {
    char *out = runCapture(src);
    nyx_test_count++;
    if (strcmp(out, expected) != 0) {
        nyx_test_failures++;
        fprintf(stderr, "  FAIL [%s]: expected %s got %s\n", name, expected, out);
    }
    free(out);
}

void run_vm_tests(void) {
    TEST_SUITE("vm (end-to-end)");

    expectOutput("arithmetic", "print(1 + 2 * 3 - 4 / 2);", "5\n");
    expectOutput("modulo", "print(17 % 5);", "2\n");
    expectOutput("string-concat", "print(\"foo\" + \"bar\");", "foobar\n");
    expectOutput("comparisons", "print(3 < 5); print(5 <= 5); print(3 > 5);", "true\ntrue\nfalse\n");
    expectOutput("logical-and-or", "print(true and false); print(false or true);", "false\ntrue\n");

    expectOutput("global-var", "var x = 10; x = x + 5; print(x);", "15\n");
    expectOutput("block-scope",
                 "var x = 1; { var x = 2; print(x); } print(x);", "2\n1\n");

    expectOutput("if-else", "if (1 < 2) { print(\"yes\"); } else { print(\"no\"); }", "yes\n");

    expectOutput("while-loop",
                 "var i = 0; var sum = 0; while (i < 5) { sum = sum + i; i = i + 1; } print(sum);",
                 "10\n");

    expectOutput("for-loop",
                 "var sum = 0; for (var i = 0; i < 5; i = i + 1) { sum = sum + i; } print(sum);",
                 "10\n");

    expectOutput("break-continue",
                 "var sum = 0; for (var i = 0; i < 10; i = i + 1) { "
                 "if (i == 5) { break; } if (i % 2 == 0) { continue; } sum = sum + i; } print(sum);",
                 "4\n"); /* 1 + 3 = 4 (evens skipped, loop breaks before 5) */

    expectOutput("function-recursion",
                 "fun fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); } "
                 "print(fib(10));",
                 "55\n");

    expectOutput("closure-counter",
                 "fun makeCounter() { var count = 0; fun counter() { count = count + 1; return count; } "
                 "return counter; } var c1 = makeCounter(); var c2 = makeCounter(); "
                 "print(c1()); print(c1()); print(c2());",
                 "1\n2\n1\n");

    expectOutput("array-index",
                 "var a = [1, 2, 3]; a[1] = 99; print(a[0]); print(a[1]); print(a[2]); print(len(a));",
                 "1\n99\n3\n3\n");

    expectOutput("map-index",
                 "var m = {\"a\": 1, \"b\": 2}; m[\"c\"] = 3; print(m[\"a\"]); print(m[\"c\"]); print(has(m, \"z\"));",
                 "1\n3\nfalse\n");

    expectOutput("push-pop",
                 "var a = []; push(a, 1); push(a, 2); print(len(a)); print(pop(a)); print(len(a));",
                 "2\n2\n1\n");

    expectOutput("type-native",
                 "print(type(1)); print(type(\"s\")); print(type(nil)); print(type([1])); print(type({\"a\":1}));",
                 "number\nstring\nnil\narray\nmap\n");

    expectOutput("str-num-native", "print(str(42)); print(num(\"3.5\") + 1);", "42\n4.5\n");

    expectOutput("nested-closures-share-upvalue",
                 "fun outer() { var x = 1; fun inc() { x = x + 1; } fun get() { return x; } "
                 "inc(); inc(); return get(); } print(outer());",
                 "3\n");
}
