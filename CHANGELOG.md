# Changelog

## 1.0.0

First stable release. The v1 scope described in `docs/roadmap.md` is complete.

- New string natives: `find(haystack, needle)` returns the first match index
  or -1, `split(string, separator)` returns an array of pieces, and
  `join(array, separator)` concatenates an array of strings. All three are
  covered by a new golden test and hold up under `NYX_GC_STRESS`.
- Version reported by the REPL banner is now 1.0.0.

## 0.1.0

- Hand-written lexer, single-pass Pratt-parsing compiler, stack-based
  bytecode VM and mark-sweep GC in C99 with zero dependencies.
- Closures with shared upvalues, arrays, string-keyed maps, `break`/`continue`.
- Native stdlib: `print`, `len`, `str`, `num`, `type`, `push`, `pop`,
  `keys`, `has`, `slice`, `assert`, `clock`.
- Unit test harness (531 checks) plus golden-file suite; CI with a
  cross-platform build+test matrix and a sanitizer job.
- Benchmarks against CPython with checked-in results.
