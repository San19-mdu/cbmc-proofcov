# Testing proof coverage

Programs for trying out and testing `--proof-coverage` beyond the regression tests in
`regression/cbmc-proof-coverage`.

| folder | what |
|---|---|
| [`examples/`](examples/README.md) | small C programs with an `assert`, each showing one behaviour of proof coverage |
| [`generator/`](generator/README.md) | a random C program generator, and a script that compares gcc's line coverage (gcov) with proof coverage on the generated programs |

Both use the proof-coverage binary built from this repository, `build/bin/cbmc` (see
[`README-RUN.md`](../README-RUN.md), steps 1–5). The commands below call it `pcbmc`, as
in step 5 there.
