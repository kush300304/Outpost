# Contributing to Outpost

Thanks for taking a look. This file explains how to build and test the project, the code style it follows, and
what a change needs before it is merged.

## Build and test

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build      # 163 tests, 5 suites
docker compose up -d --build --wait && scripts/integrationTest.sh            # end to end
```

The README's *How to test it yourself* table lists every other suite: sanitizers, chaos test, benchmarks,
dashboard checks, shutdown test. [docs/handsOnTour.md](docs/handsOnTour.md) walks through all of them step by
step.

## Code style

Most of these rules are checked automatically by `scripts/checkStyle.py` and `scripts/format.sh --check`; CI
runs both on every push.

- **Layout:** for every module and service, headers go in `include/<module>/`, sources in `src/` and tests in
  `tests/`.
- **Naming:**
  - files `lowerCamelCase` (`consistentHash.h`);
  - classes `PascalCase`;
  - variables and functions `camelCase`.
- **Formatting:** follow the checked-in `.clang-format`. Allman braces: every block's opening brace is on its own
  line, except a lambda body passed as an argument. The column limit is 100. Run `scripts/format.sh` before
  committing C++.
- **File header:** every source file starts with a `/// @file` comment that says what the file is.
- **Single exit point:** a function has at most one `return`, as its last statement, using an explicit result
  variable rather than early returns.
- **Small surface:** keep logic inside one function. Add a helper only when it is used in more than one place.
- **Memory and I/O:**
  - RAII and smart pointers only; no raw `new` or `delete`;
  - every external call (HTTP, disk) has a timeout or a bounded wait;
  - never hold a lock while waiting on the network or an fsync.
- **Comments:** describe the code as it is now. No history ("fixed", "previously", "changed from"); that
  belongs in commit messages.

## The hand-built core

The primitives in `common/` (consistent hash ring, LRU cache, token bucket, circuit breaker, write-ahead log
and their helpers) use **no third-party libraries**, and `common/CMakeLists.txt` links only the system thread
library. The services may use the three libraries already in the build: cpp-httplib, nlohmann/json and spdlog.
Discuss any new dependency in an issue first.

## Changes and commits

- **Test first.** A bug fix or feature starts with a test that fails for the right reason. Then make it pass,
  and run the whole suite. For a data race, the failing evidence can be a ThreadSanitizer report; for a
  performance change, a measurement.
- **One logical change per commit,** with a message in the imperative mood ("Add purge fan-out to the router").
  Never commit build directories (`build*/`).
- **Concurrency changes** must also pass the sanitizer builds (`-DOUTPOST_SANITIZE=thread` and
  `address,undefined`).
- **Update the docs** a change affects: `docs/statsApi.md` (a test reads its field table), the README, and
  `docs/architecture.md`.

## Numbers are measured, never estimated

Every number in the README and `docs/` comes from a real run, saved under `benchmarks/results/` or `docs/gates/`
with the hardware it ran on. If you change performance, rerun `scripts/runBenchmark.sh` and quote the new run.
Use `RATE=<n>` to compare two builds at the same load.
