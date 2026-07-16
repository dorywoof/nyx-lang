# Roadmap

v1 (this repository, as committed) is a complete, tested, benchmarked
bytecode VM: lexer, single-pass compiler, stack-based VM, mark-sweep GC,
closures/upvalues, arrays, string-keyed maps, a small native stdlib, a unit
+ golden-file test suite, and CI. Everything below is deliberately *not* in
v1 -- either out of scope for the timeline, or waiting on a design decision
that deserved more thought than "just ship it."

## Near-term (language completeness)

- **Classes and methods.** The single biggest missing "expected" language
  feature. Needs: a `class` declaration form, method tables per class,
  `this` binding, and a decision on single vs. no inheritance. clox's
  `OP_METHOD`/`OP_INVOKE`/bound-method design is the natural reference.
- **Exceptions (`try`/`catch`/`throw`).** Needs a second kind of
  non-local control flow alongside `return` -- either an exception stack
  the VM unwinds on `throw`, or (simpler, slower) representing errors as
  values and threading them through explicitly first, then adding sugar.
- **A real standard library surface.** Current natives (`print`, `len`,
  `str`, `num`, `type`, `push`/`pop`, `keys`/`has`, `slice`,
  `find`/`split`/`join`, `assert`, `clock`) cover what the test suite,
  benchmarks and everyday string handling needed. Missing: math functions
  beyond arithmetic, file I/O, and a real error-message-friendly `assert`.
- **A module system (`import`).** Right now every `.nyx` file is the whole
  program. Multi-file programs need a way to compile and link more than
  one `ObjFunction` graph together, plus a decision about whether modules
  get their own global namespace or share one.

## Performance

- **Peephole optimizer.** A post-compile pass over each `Chunk` that
  collapses obviously redundant instruction sequences (e.g.
  `CONSTANT x; CONSTANT y; ADD` where both are literals -> `CONSTANT x+y`,
  constant folding). This is the concrete feature that would justify
  introducing an IR between parsing and bytecode (see
  `docs/architecture.md`'s single-pass-compilation tradeoff).
- **Inline caching for globals.** `OP_GET_GLOBAL`/`OP_SET_GLOBAL` do a
  full hash lookup every time; caching the last-resolved slot per call site
  is the standard fix once profiling shows it matters.
- **A generational GC.** The current collector is mark-sweep,
  non-generational: every collection walks every live object. Most
  allocations die young (temporary strings, short-lived arrays); a young
  generation collected far more often than the old one would cut pause
  times substantially. This is a bigger change than it sounds -- it needs
  write barriers everywhere an old object can be made to point at a young
  one.

## Tooling

- **REPL line editing.** The current REPL is `fgets()` on one line at a
  time -- no history, no arrow-key editing, no multi-line input. A real
  REPL needs a line-editing library (or a hand-rolled raw-mode terminal
  reader) and support for incomplete-expression continuation.
- **Better runtime error messages.** Errors report a line number and a
  flat call-stack dump; they don't point at the exact source column, and
  they don't suggest fixes (e.g. "did you mean `x`?" for a typo'd
  identifier).
- **An optional static type-checking pass.** Nyx is dynamically typed by
  design (matches the "explainable in an interview" and "buildable in
  months" constraints from the original project brief), but a lightweight
  *optional* type-annotation + checker pass (gradual typing, TypeScript- or
  Sorbet-style) is a natural "if I had another semester" extension that
  would also be a good excuse to build a proper type-inference algorithm.

## Explicitly out of scope for this project

- A JIT. Interesting, but "explainable under interview questioning" and
  "JIT compiler" don't really go together on this timeline -- noted here
  so it's clear this was a deliberate scope cut, not an oversight.
- Concurrency/threads. Nyx's VM is single-threaded by design; there's no
  green-thread scheduler, no `async`, nothing. Out of scope for v1 and
  every near-term item above.
