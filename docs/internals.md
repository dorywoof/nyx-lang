# Internals

A guided tour of the source tree, file by file: what each module holds,
which data structures matter, and how to trace a program through it.

This is the "how it works" document. The "why it was built this way"
document is [architecture.md](architecture.md), which covers the design
decisions (single-pass compilation, stack-based VM, mark-sweep GC,
tagged-union values, one shared hash table) and is not repeated here.
The instruction set itself is listed in [bytecode.md](bytecode.md).

## Reading order

The modules depend on each other roughly in this order, and reading them
in it means never hitting a type you have not seen yet:

```
common.h -> value.h -> object.h -> chunk.c -> table.c
         -> scanner.c -> compiler.c -> vm.c -> gc.c
```

`src/main.c` is the entry point and the shortest useful file to start
from: it decides between running a script, running the REPL, and
disassembling (`--disasm`) a file without executing it.

## Tracing one expression

Everything below is easier to follow with a concrete example in mind.
Take `1 + 2 * 3`.

The scanner produces `NUMBER(1) PLUS NUMBER(2) STAR NUMBER(3)`. The
compiler parses those with precedence climbing, so `2 * 3` is fully
parsed as the right-hand side of `+` before `+` itself is emitted. The
resulting bytecode is:

```
CONSTANT 1
CONSTANT 2
CONSTANT 3
MULTIPLY
ADD
```

The VM executes that against its operand stack: push 1 `[1]`, push 2
`[1 2]`, push 3 `[1 2 3]`, `MULTIPLY` pops 3 and 2 and pushes 6 `[1 6]`,
`ADD` pops 6 and 1 and pushes 7 `[7]`.

Nothing here has to be taken on trust -- `nyx --disasm file.nyx` prints
the bytecode the compiler actually produced, via `src/debug/debug.c`.

## Scanner (`src/lexer/scanner.c`)

Turns characters into tokens, one at a time on demand: `scanToken()` is
called by the compiler whenever it needs the next token, so the file is
never tokenized up front and no token array is ever materialized.

Each token carries the source line it came from, which is what lets
runtime errors report a line number rather than failing anonymously.

Words are scanned as identifiers first; `identifierType()` then checks
the text against a hand-written switch over the keyword set (`and`, `if`,
`while`, and the rest) and reclassifies it if it matches. For a small
fixed keyword set that beats a hash lookup, and it keeps the whole
keyword table visible in one function.

## Compiler (`src/compiler/compiler.c`)

The largest file in the project, at roughly a quarter of the codebase.
It is a recursive-descent parser for statements combined with a Pratt
parser for expressions, emitting bytecode directly as it parses.

**Expressions.** Every token that can start an expression (a literal, an
identifier, `(`, `-`) has a prefix handler. Every token that can continue
one (`+`, `*`, `(` for a call, `[` for a subscript) has an infix handler
and a precedence. `parsePrecedence()` calls the prefix handler once, then
keeps consuming infix handlers while the next operator binds tightly
enough. That one loop, driven by the `rules[]` table, does the work that
would otherwise need a separate function per precedence level.

Left associativity falls out of the loop condition: it continues while
the next operator's precedence is greater than or equal to the current
one, so `1 - 2 - 3` groups as `(1 - 2) - 3`. A right-associative
operator would need a strictly-greater test instead; the language has
none at present.

**Local variables.** The compiler keeps a `locals[]` array at compile
time mapping each declared local to a stack slot. On a variable
reference, `resolveLocal()` searches that array and emits
`OP_GET_LOCAL <slot>` on a hit -- a direct index at runtime, with no
name involved. Only names that fail to resolve as locals fall back to
`OP_GET_GLOBAL`, which does a hash lookup in `vm.globals` on every
execution. This is why locals are cheaper than globals here, as in most
bytecode VMs.

**Closures.** The compile-time bookkeeping that decides which locals get
captured by which nested function, and the open/closed upvalue split at
runtime, are described in
[architecture.md](architecture.md#closures-and-upvalues). This is the
part of the compiler where a mistake does not crash -- it silently
produces the wrong answer -- so it is the part worth reading twice.

## Chunk and bytecode (`src/vm/chunk.c`, `include/nyx/opcodes.h`)

A `Chunk` is one compiled function body: a flat byte array of
instructions, a parallel array of line numbers for error reporting, and
a constant pool.

The constant pool exists because instructions are one byte plus small
fixed operands, while a `double` is eight bytes and a string is
arbitrary length. Neither fits inline, so the value is stored once in
the pool and the instruction carries a one-byte index into it.

## Values and objects (`src/vm/value.c`, `src/vm/object.c`)

`Value` is a tagged union: a type tag (`nil`, `bool`, `number`, `obj`)
plus a union holding the payload. Everything heap-allocated -- strings,
functions, closures, upvalues, arrays, maps -- is a `Value` of type
`obj` pointing at an `Obj`.

Every heap type begins with the same `Obj` header (`type`, `isMarked`,
`next`). That shared prefix is what lets the collector walk a single
linked list of every object ever allocated without knowing any concrete
type in advance, and it is the reason the sweep phase is as short as it
is.

## Hash table (`src/vm/table.c`)

Open addressing with linear probing, serving three jobs: global
variables, the string interner, and every user-facing map.

**Tombstones.** Deleting an entry cannot simply blank the slot. Linear
probing finds displaced entries by walking forward from their home
bucket until it hits an empty slot, so blanking a slot in the middle of
such a run would truncate the search and make later lookups wrongly
report a miss. A delete therefore leaves a tombstone (`key == NULL` with
a `true` value) meaning "keep probing past me", which a later insert can
reuse.

**Growth.** `adjustCapacity()` doubles the table once it exceeds
`TABLE_MAX_LOAD`, defined as 0.75. Higher load factors mean longer probe
chains and slower lookups; lower ones waste memory.

## VM (`src/vm/vm.c`)

`run()` is a `for (;;)` loop around a switch on the current opcode. It
maintains an operand stack (`vm.stack`) for intermediate values and a
fixed array of call frames (`vm.frames`).

**Calls.** On `OP_CALL` the arguments are already sitting on the stack
below the callee. `call()` pushes a `CallFrame` whose `slots` pointer
aims directly at those slots, so parameter 0 is local slot 1 and nothing
is copied, then the loop continues from the callee's first instruction.
A call allocates no heap memory.

**Recursion limit.** `vm.frames` is `NYX_MAX_CALL_FRAMES` (256) entries,
fixed rather than growable. Exceeding it produces a clean "Stack
overflow." runtime error instead of a crash.

**Errors.** `runtimeError()` prints the message followed by a walk of
every active call frame, then empties the stack. It is called from
inside opcode handlers, which is where messages like "Operands must be
numbers." originate.

## Garbage collector (`src/memory/memory.c`, `src/memory/gc.c`)

Every heap object starts unmarked. When allocation crosses a threshold,
a collection runs in three phases.

**Mark.** Start from the roots and mark everything directly reachable.
`markRoots()` in `gc.c` is the definitive list: every slot between
`vm.stack` and `vm.stackTop`, every closure on the call-frame stack,
every open upvalue, the whole `vm.globals` table, and -- via
`markCompilerRoots()` -- anything the compiler is still holding if a
collection happens mid-compile. That last root is easy to forget and
genuinely necessary: compilation itself allocates, for instance for
every string literal, and those allocations are not yet reachable from
the VM stack.

**Trace.** An object that holds other objects (a closure holds its
function and upvalues, an array holds its elements) needs those marked
too. This runs off a worklist, `grayStack`, rather than recursion, so a
deeply nested structure cannot overflow the C call stack.

**Sweep.** Walk `vm.objects`, the linked list of every allocation, and
free everything still unmarked.

**Triggering.** `vm.bytesAllocated` is updated on every allocation and
free, all of which are routed through the single `reallocate()`
function. When it passes `vm.nextGC` a collection runs, after which
`nextGC` is set to twice the post-collection live size. The threshold
therefore scales with the program's actual working set instead of firing
constantly once a program legitimately holds a lot of memory.

**The string interner is not a root.** `vm.strings` holds every distinct
string ever created so that identical strings share one allocation
(`copyString()` consults it before allocating). If it were treated as a
normal root, no string could ever be collected, because all of them
would stay permanently reachable from it. Instead
`tableRemoveWhiteKeys()` runs after tracing but before the sweep,
dropping interned strings that nothing else references.

## Verifying the collector

Memory-management bugs are the ones least likely to show up in ordinary
testing, so the project checks for them two ways, both reproducible from
a clean checkout.

`NYX_GC_STRESS` compiles in a forced full collection before every single
allocation. A root the mark phase fails to trace then produces a
use-after-free almost immediately, rather than only under memory
pressure with some particular allocation pattern.

`NYX_SANITIZE` adds AddressSanitizer and UndefinedBehaviorSanitizer.
Built together with GC stress, the full test suite and every example
script run clean, and ASan aborts precisely on any use-after-free,
double-free, or out-of-bounds access.

Both configurations run in CI on every push, in the `sanitize` job.

## Performance notes

The benchmark table and its methodology are in the
[README](../README.md#benchmarks). Two results are worth understanding
rather than just quoting.

Nyx beats CPython modestly on recursive calls and on a bubble sort. That
is the expected shape: both are dominated by call overhead and by
arithmetic on locals, which compile to direct stack-slot indexing here.

Nyx loses badly -- by roughly thirty times -- on naive string
concatenation in a loop. CPython special-cases `s = s + x` when nothing
else holds a reference to the old string, resizing the existing buffer
in place instead of allocating a fresh string per iteration, which turns
an apparently quadratic loop into amortized linear time. Nyx has no
equivalent optimization and no mutable string builder, so it pays the
full quadratic cost. It is listed in [roadmap.md](roadmap.md) rather
than hidden.
