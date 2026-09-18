# Architecture

## Pipeline

```
source (.nyx text)
      |
      v
+-----------+     scanToken() called on demand, not all at once --
|  Scanner  |     the parser pulls one token at a time (src/lexer/scanner.c)
+-----------+
      |  Token stream
      v
+----------------------------+
|  Parser + Compiler          |   single pass: recursive-descent statements +
|  (src/compiler/compiler.c)  |   Pratt-parsed expressions, emitting bytecode
+----------------------------+   directly -- there is no separate AST walked
      |                          in a second pass (see "single-pass" below)
      |  Chunk (bytecode + constant pool + line numbers)
      v
+-----------+
|    VM     |   stack-based bytecode interpreter, one switch-dispatched
| (vm.c)    |   loop per call to run()
+-----------+
      |
      v
   program output / runtime errors
```

Compiling and running are two separate phases at the top level
(`interpret()` in vm.c calls `compile()` to completion, then `run()`), but
*inside* compilation there's no AST -- see below.

## Why single-pass compilation (no separate AST)

A more "traditional" compiler pipeline is source -> tokens -> AST -> bytecode
(two passes over the program structure: build the tree, then walk it to
emit code). Nyx's compiler instead emits bytecode directly as it parses --
`binary()` parses the right operand *and* emits the operator's opcode in the
same function call. This is less flexible (no tree to run optimization
passes over before code generation) but it's simpler to get right, uses
less memory per compile, and is fast enough that Nyx doesn't need a
separate optimization pass for the programs it's meant to run. The tradeoff
is written down here on purpose: `docs/roadmap.md`'s peephole-optimizer
item would be the first thing that pushes this design toward needing an
intermediate representation.

## Why a stack-based VM, not register-based

Two mainstream designs for a bytecode VM:

- **Stack-based** (what Nyx does): every instruction takes its operands
  from the top of an operand stack and pushes its result back. `1 + 2`
  compiles to `CONSTANT 1; CONSTANT 2; ADD` -- three instructions, no
  operand fields needed for where the values come from.
- **Register-based** (what Lua has used since 5.0; CPython is stack-based
  like Nyx): each function has a bank of "registers" (really just stack
  slots addressed by number), and instructions name their operands
  explicitly, e.g. `ADD r2, r0, r1`. Fewer instructions execute per
  operation (no separate push/pop), but each instruction is bigger and the
  compiler has to do register allocation.

Nyx is stack-based because the compiler is dramatically simpler to write
correctly -- there's no register allocator, no register-pressure
bookkeeping, and expression compilation is a direct, obvious translation of
the Pratt parser's recursion. That simplicity is worth more here than the
(real, measurable) performance register-based bytecode would buy, given
that program correctness and explainability were the actual goals.

## Why mark-sweep GC, not reference counting

Answered where it's implemented: see the comment block at the top of
`src/memory/gc.c`. Short version: refcounting can't free cycles on its own,
and closures over mutually-referencing state make cycles trivial to create
by accident; a collector that always eventually reclaims cycles beats one
that "mostly" does, at the cost of a periodic stop-the-world pause.

## Why a tagged union `Value`, not NaN-boxing

Answered in `include/nyx/value.h`. Short version: NaN-boxing (packing every
value into the bit pattern of a 64-bit double) roughly halves memory per
value and removes a branch from every type check, at the cost of code that
needs a diagram to read. A tagged `struct { ValueType type; union {...} as; }`
costs 16 bytes instead of 8 per `Value` but is legible top to bottom, which
mattered more for a project whose whole point is to be defensible under
questioning.

## Why one `Table` (hash table) implementation for three different jobs

`include/nyx/table.h`'s `Table` backs: (1) `vm.globals`, the global
variable namespace; (2) `vm.strings`, the string interner (see below); and
(3) every `ObjMap` a Nyx program creates. Three different roles, one
implementation, because they're structurally the same problem
(string-keyed, open-addressed hash table) and Nyx doesn't need three
different performance profiles for it. The coupling this creates -- e.g.
`ObjMap` and the string interner sharing `tableSet`'s tombstone-on-delete
behavior -- is a deliberate small-project simplification, not an oversight;
a production language would likely give the user-facing map type its own
tuning (different load factor, maybe open-addressing vs. chaining) once
profiling justified it.

## Why maps are string-keyed only

`ObjMap` wraps a `Table`, and `Table` only knows how to hash/compare
`ObjString*` keys. A more general map (arbitrary-Value keys, like Python
dicts) needs a `hashValue(Value)`/`valuesEqual`-based generic table instead
of the string-specific one Nyx reuses from the globals table and string
interner. That's a real feature gap, tracked in `docs/roadmap.md`, traded
deliberately for reusing one well-tested hash table implementation across
three subsystems instead of writing (and testing) a second one.

## String interning

Every `ObjString` Nyx creates goes through `copyString()`/`takeString()`
(`src/vm/object.c`), which first checks `vm.strings` (a `Table` used purely
as a hash-set) for an existing string with the same characters. If found,
the existing pointer is reused instead of allocating a new one. Two
consequences: (1) `==` on strings is pointer comparison (`valuesEqual()` in
value.c), which is O(1) instead of O(n) `memcmp`; (2) `vm.strings` must
*not* be a normal GC root, or every string ever created would stay
reachable forever -- that's why `collectGarbage()` calls
`tableRemoveWhiteKeys(&vm.strings)` *after* tracing real references but
*before* sweeping, dropping interner entries for strings nothing else
points to anymore.

## Closures and upvalues

A closure needs to keep working after the function that declared its
captured variables has returned -- but locals normally live on the VM stack,
which gets reused the moment a call returns. Nyx's answer (the same one
Lua and clox use): an `ObjUpvalue` starts **open**, pointing directly at
the live stack slot, so reads/writes through it and reads/writes to the
local itself stay in sync automatically. When the function that owns that
stack slot returns, `closeUpvalues()` (`vm.c`) copies the value out of the
stack into the `ObjUpvalue` itself (`upvalue->closed`) and repoints
`location` at that copy -- the upvalue is now **closed** and keeps working
independent of the stack. Two closures capturing the *same* local share the
*same* `ObjUpvalue` object (`captureUpvalue()` searches a sorted linked
list of currently-open upvalues before creating a new one), which is why
`tests/lang/closures.nyx`'s counter example works: every call to the
returned closure mutates the one shared upvalue.

## Call frames

`vm.frames[]` is a fixed-size array of `CallFrame` (closure + instruction
pointer + a `slots` pointer into `vm.stack`), not a heap-allocated linked
list -- function calls in Nyx don't allocate. `NYX_MAX_CALL_FRAMES` (256)
is therefore also Nyx's maximum recursion depth; exceeding it is a
"Stack overflow." runtime error, not a crash.
