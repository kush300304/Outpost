## What and why

<!-- What does this change do, and what problem does it solve? -->

## How it was tested

- [ ] A test that failed before this change and passes now (or why none is possible)
- [ ] `ctest --test-dir build` passes
- [ ] Sanitizer builds pass, if the change touches concurrency or memory (`-DOUTPOST_SANITIZE=thread`, `address,undefined`)
- [ ] `python3 scripts/checkStyle.py` and `scripts/format.sh --check` report nothing
- [ ] `scripts/integrationTest.sh` passes, if behaviour across services changed
- [ ] Docs updated (README, `docs/architecture.md`, `docs/statsApi.md`) where behaviour changed
- [ ] Any number quoted comes from a real run saved under `benchmarks/results/`
