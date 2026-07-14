# Bytecode reference

Every opcode is one byte (`OpCode` in `include/nyx/opcodes.h`). Most take
zero or more operand bytes immediately following them in the `Chunk`'s
`code` array; the VM's dispatch loop (`run()` in `src/vm/vm.c`) is a single
`switch` over these values, one `case` per opcode.

Run `nyx --disasm <script>.nyx` to see exactly what a given program compiles
to -- that's the fastest way to build intuition for this table.

| Opcode | Operands | Stack effect | Notes |
|---|---|---|---|
| `OP_CONSTANT` | 1 byte: constant pool index | `... -> ..., value` | Pushes `chunk->constants[index]`. |
| `OP_NIL` / `OP_TRUE` / `OP_FALSE` | none | `... -> ..., v` | Push the literal. |
| `OP_POP` | none | `..., a -> ...` | Discards the top of stack (every expression-statement ends with one). |
| `OP_DUP` | none | `..., a -> ..., a, a` | Duplicates the top value. Currently unused by the compiler but kept for `docs/roadmap.md` features that need it (e.g. postfix `++`). |
| `OP_GET_LOCAL` / `OP_SET_LOCAL` | 1 byte: stack slot | reads/writes `frame->slots[slot]` | Locals live directly on the VM stack -- no heap allocation, no lookup by name. |
| `OP_GET_GLOBAL` / `OP_DEFINE_GLOBAL` / `OP_SET_GLOBAL` | 1 byte: constant index (name) | reads/writes `vm.globals` | Globals are looked up by name in a hash table every time -- the classic locals-vs-globals speed gap in a tree of bytecode VMs. |
| `OP_GET_UPVALUE` / `OP_SET_UPVALUE` | 1 byte: upvalue index | reads/writes `*frame->closure->upvalues[index]->location` | See "closures" in `docs/architecture.md`. |
| `OP_GET_INDEX` | none | `..., container, index -> ..., value` | Dispatches on `container`'s runtime type (array/map/string). |
| `OP_SET_INDEX` | none | `..., container, index, value -> ..., value` | Leaves `value` on the stack so `a[i] = x` itself evaluates to `x`. |
| `OP_EQUAL` / `OP_GREATER` / `OP_LESS` | none | `..., a, b -> ..., bool` | `!=`, `<=`, `>=` are synthesized by the compiler as `==`/`<`/`>` followed by `OP_NOT` (see `binary()` in compiler.c) rather than getting their own opcodes. |
| `OP_ADD` | none | `..., a, b -> ..., a+b` | Polymorphic: number+number or string+string; anything else is a runtime error. |
| `OP_SUBTRACT` / `OP_MULTIPLY` / `OP_DIVIDE` / `OP_MODULO` | none | `..., a, b -> ..., a op b` | Numbers only. |
| `OP_NOT` / `OP_NEGATE` | none | `..., a -> ..., r` | `!` and unary `-`. |
| `OP_JUMP` | 2 bytes: forward offset | none | Unconditional relative jump. |
| `OP_JUMP_IF_FALSE` | 2 bytes: forward offset | none (does not pop) | Leaves the condition value on the stack -- `if`/`while`/`and`/`or` each emit their own explicit `OP_POP` afterward, which is what makes short-circuiting work. |
| `OP_LOOP` | 2 bytes: backward offset | none | Same encoding as `OP_JUMP` but subtracts instead of adds; this is what `while`/`for`/`continue` compile to. |
| `OP_CALL` | 1 byte: argument count | `..., callee, arg0..argN -> ..., result` | Handles both `ObjClosure` and `ObjNative` callees (`callValue()`). |
| `OP_CLOSURE` | 1 byte: constant index (an `ObjFunction`), then 2 bytes per upvalue (`isLocal`, `index`) | `... -> ..., closure` | The only opcode with a variable-length operand list -- the disassembler (`closureInstruction()` in debug.c) has to know this specially. |
| `OP_CLOSE_UPVALUE` | none | `..., a -> ...` | Detaches any open upvalue pointing at the top stack slot before popping it (see "closures" in architecture.md). |
| `OP_RETURN` | none | pops the whole call frame | Always pops exactly one value as the return value; a bare `return;` first pushes `nil`. |
| `OP_ARRAY` | 1 byte: element count N | `..., v0..vN-1 -> ..., array` | Pops N values, builds an `ObjArray` from them in order. |
| `OP_MAP` | 1 byte: pair count N | `..., k0,v0..kN-1,vN-1 -> ..., map` | Pops 2N values, builds an `ObjMap`. Every key must be a string at runtime or this is an error. |

## Why relative jump offsets, not absolute addresses

`OP_JUMP`/`OP_JUMP_IF_FALSE`/`OP_LOOP` all encode *offsets from the
instruction after the jump*, not absolute bytecode positions. That's what
lets the compiler use backpatching (`emitJump()` writes a placeholder
`0xffff`, and `patchJump()` fixes it up once the jump target is known) --
useful because when compiling `if (cond) { ... }` the compiler doesn't know
how big the `then` branch is until it has already finished compiling it.
