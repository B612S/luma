# Linting with clang-tidy

Luma has an opt-in, correctness-focused clang-tidy profile in
[.clang-tidy](../.clang-tidy). It is not part of a normal build and requires
`clang-tidy`, `run-clang-tidy` and Python 3 (run-clang-tidy itself is a Python
script).

## Running

```bash
cmake -B build -S . -DLUMA_CLANG_TIDY=ON
cmake --build build --target lint
```

The `lint` target builds `luma` first, because some sources include generated
Qt headers, and then analyzes every translation unit of `main.cpp` and `core/`
once. `-DLUMA_LINT_JOBS=8` limits parallelism (the default, `0`, uses all
CPUs); the first run can take a few minutes.

Several CMake targets compile the same core sources (`luma`, `luma_testcore`,
`tst_media_open`). `tools/prepare-tidy-db.py` deduplicates
`build/compile_commands.json` into `build/tidy/compile_commands.json` so a file
is not analyzed two or three times.

## Scope

- `clang-analyzer-*`, `clang-diagnostic-*`, `bugprone-*`, `performance-*` and
  `portability-*`, plus a small set of `modernize-*`, `misc-*` and
  `readability-*` checks that tend to catch real problems.
- Vendored `mpvqt/` is exempt from style checks through
  [mpvqt/.clang-tidy](../mpvqt/.clang-tidy): only the static analyzer and
  compiler diagnostics run there. This keeps upstream diffs small, as required
  by `mpvqt/README.md`.
- Tests are not part of the `lint` target, which avoids building every test
  binary just to lint. Build them first, then analyze them manually with the
  same compilation database.
- Style checks that mostly restate Qt's conventions are disabled so the target
  stays actionable: braces around single statements, `modernize-use-auto`,
  `modernize-use-nodiscard`, `misc-const-correctness`,
  `modernize-avoid-c-style-cast`, `bugprone-signed-bitwise`, and similar. A
  full style sweep is a separate, deliberate change, not something every lint
  run should demand.

To try a stricter sweep, run `run-clang-tidy` directly with an explicit
`-checks` filter after the `lint` target has created
`build/tidy/compile_commands.json`:

```bash
run-clang-tidy -p build/tidy -j 8 \
  -removed-arg=-mno-direct-extern-access \
  -checks='-*,modernize-*' 'core/'
```

## Known false positives

Both cases below are annotated with `NOLINT` at the diagnostic site, so the
`lint` target is expected to finish without warnings. Remove the annotation if
the surrounding code changes.

- `clang-analyzer-cplusplus.NewDeleteLeaks` on `QFutureWatcher` objects: the
  watcher is parented to `this` and its `finished` handler calls
  `deleteLater()`. The analyzer does not model Qt's signal connections.
- `bugprone-return-const-ref-from-parameter` on the `QDBusArgument` stream
  operators: returning the argument is the signature Qt requires, and the
  returned reference refers to the caller's lvalue.
