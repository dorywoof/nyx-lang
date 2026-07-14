# nyx-lang

A small dynamically-typed scripting language: a hand-written lexer, a
single-pass Pratt-parsing compiler, a stack-based bytecode virtual machine,
and a mark-sweep garbage collector -- all written from scratch in C99, with
zero third-party dependencies.

```
$ nyx
nyx 0.1.0 -- Ctrl+D (or Ctrl+Z on Windows) to exit
> fun fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }
> print(fib(20));
6765
```

## Why I built this

I wanted one project that actually exercises the core of computer science
end to end -- parsing theory, data structures, and systems-level memory
management -- in something small enough for one person to build,
understand completely, and defend under questioning, rather than something
that's impressive mainly because of its size. A bytecode-compiled
scripting language hits all three: you can't fake understanding a garbage
collector, and you can't fake understanding why a closure needs an
upvalue.

## Language features

| Feature | Example |
|---|---|
| Numbers, strings, booleans, `nil` | `var x = 3.5; var s = "hi"; var b = true;` |
| Arithmetic, comparison, logic | `1 + 2 * 3 % 4`, `a and b or !c` |
| Variables, block scoping | `var x = 1; { var x = 2; }` |
| Control flow | `if/else`, `while`, `for`, `break`, `continue` |
| Functions, recursion | `fun fib(n) { if (n < 2) { return n; } return fib(n-1)+fib(n-2); }` |
| Closures (with shared upvalues) | `fun counter() { var n = 0; fun bump() { n = n + 1; return n; } return bump; }` |
| Arrays | `var a = [1, 2, 3]; a[0] = 9; push(a, 4);` |
| Maps (string-keyed) | `var m = {"a": 1}; m["b"] = 2; has(m, "a");` |
| Native stdlib | `print`, `len`, `str`, `num`, `type`, `push`, `pop`, `keys`, `has`, `slice`, `assert`, `clock` |

See `examples/` for a couple of small complete programs, and
`docs/grammar.ebnf` for the full grammar.

## Architecture

```
source --scan--> tokens --compile (single pass)--> bytecode --run--> output
```

Full writeup, including every non-obvious design decision and why it was
made: **[docs/architecture.md](docs/architecture.md)**. Bytecode
instruction reference: **[docs/bytecode.md](docs/bytecode.md)**.

### Design decisions at a glance

- **Stack-based VM, not register-based.** Simpler compiler, some
  performance left on the table versus what CPython/Lua do. ([more](docs/architecture.md#why-a-stack-based-vm-not-register-based))
- **Mark-sweep GC, not reference counting.** Reference counting can't
  free cycles on its own; closures make cycles easy to create by accident.
  ([more](docs/architecture.md#why-mark-sweep-gc-not-reference-counting))
- **Tagged-union `Value`, not NaN-boxing.** Costs 16 bytes instead of 8
  per value; stays readable without a bit-layout diagram. ([more](docs/architecture.md#why-a-tagged-union-value-not-nan-boxing))
- **Maps are string-keyed only.** Reuses one hash table implementation
  across globals, the string interner, and user maps instead of writing a
  second general-purpose one. ([more](docs/architecture.md#why-maps-are-string-keyed-only))

## Benchmarks

Methodology: each benchmark times itself in-process (`clock()` in Nyx,
`time.perf_counter()` in Python), which excludes interpreter/process
startup on both sides equally. Median of 5 runs. Run it yourself:
`python3 benchmarks/bench.py <path-to-nyx-binary>`.

| Benchmark | Nyx (median, 5 runs) | CPython 3.13 (median) | Ratio (Python / Nyx) |
|---|---|---|---|
| `fib(32)`, naive recursion | 226.0 ms | 253.4 ms | 1.12x -- Nyx faster |
| bubble sort, n=2000 | 210.0 ms | 309.3 ms | 1.47x -- Nyx faster |
| naive string concat, n=20000 | 225.0 ms | 6.6 ms | 0.03x -- **Python faster** |

That last row is real, and worth reading rather than hiding: CPython has a
specific internal optimization where `s = s + x` in a loop resizes the
existing string buffer in place (when nothing else holds a reference to the
old string) instead of allocating a new string every iteration, turning an
apparently-O(n²) loop into amortized O(n) in practice. Nyx has no
equivalent optimization (no mutable string-builder type at all yet -- see
`docs/roadmap.md`), so it pays the full O(n²) cost every time. A
believable benchmark table has losses in it.

## Building

Requires a C99 compiler and CMake >= 3.16.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/nyx path/to/script.nyx      # run a script
./build/nyx                         # REPL
./build/nyx --disasm path/to/script.nyx   # print compiled bytecode
```

Useful CMake options (`-D<option>=ON`):

| Option | Effect |
|---|---|
| `NYX_SANITIZE` | Build with AddressSanitizer + UndefinedBehaviorSanitizer |
| `NYX_GC_STRESS` | Force a full GC collection before every single allocation |
| `NYX_GC_LOG` | Log every GC mark/sweep/alloc decision to stderr |

No local compiler? This repo was developed and verified using
[Zig](https://ziglang.org/)'s bundled C compiler (`zig cc`), which needs no
separate install beyond Zig itself and works identically on Windows/macOS/Linux:

```sh
zig cc -std=c99 -Wall -Wextra -Iinclude -O2 src/**/*.c src/main.c -lm -o nyx
```

## Testing

```sh
cmake --build build --target nyx_unit_tests
ctest --test-dir build --output-on-failure
```

This runs two suites: a custom C unit-test harness (`tests/test_*.c`) over
the scanner, chunk/bytecode layer, hash table, and end-to-end VM behavior;
and, if Python 3 is available, a golden-file suite
(`tests/run_golden.py`) that runs every script in `tests/lang/*.nyx`
through the built interpreter and diffs its output against a checked-in
`.expected` file.

Current status on this machine: **531/531 unit checks pass, 5/5 golden
tests pass**, and the full suite plus every example script also passes
clean under `-fsanitize=address,undefined` with `NYX_GC_STRESS` forcing a
collection before every allocation.

## Roadmap

v1 is complete and tested as described above. What's next, and why it's
not in v1 yet, is tracked in **[docs/roadmap.md](docs/roadmap.md)**:
classes/methods, exceptions, a module system, a peephole optimizer,
inline caching for globals, a generational GC, and REPL line editing are
the near-term items.

## License

MIT -- see [LICENSE](LICENSE).
