# Random program generator

`gen.py` generates random small C functions; `run.py` runs each one with concrete
inputs, both natively with gcc's line coverage (gcov) and with CBMC proof coverage, and
reports every line where the two differ.

Each program is a function `unsigned f(unsigned a, unsigned b, unsigned c)` of about
80 lines, plus a helper `h`. It is integer only and free of undefined behaviour (all
values are `unsigned`, divisors are made odd, shifts are by constants below 4, array
indices are masked with `& 3`, loops run at most 4 times), and uses `if`/`else`, `for`
with `break`/`continue`, `switch` with fall-through, `goto`, early `return`, `?:`, `&&`,
`||` and statements spread over two lines. The same seed always gives the same program.

## Requirements

- Python 3 (standard library only)
- `gcc` and `gcov` (`sudo apt install gcc`; gcov comes with gcc)
- the proof-coverage CBMC built from this repository (`README-RUN.md`, steps 1–3)

`run.py` uses `../../build/bin/cbmc` (relative to this folder). To use another binary,
set `CBMC=/path/to/cbmc`.

## Step by step

**1. Look at a generated program** (seed 7):

```console
python3 gen.py 7
```

**2. Run a comparison.** `run.py` writes its results into the folder you run it from, so
start from an empty folder outside the repository:

```console
mkdir -p ~/proofcov-gen && cd ~/proofcov-gen
python3 <source folder>/testing/generator/run.py 1 20 2
```

The arguments are the first seed, the number of programs and the number of random
inputs per program (default 2). `1 20 2` runs seeds 1–20 with 2 inputs each, i.e. 40
tests, in parallel on all CPU cores (about 5 seconds on 16 cores). It prints:

```
40 tests in 5s
status: pass 40
tests with proof-only lines, by kind: case label 9, no gcc code 7, branch merge 1
```

For a larger run, e.g. 1,500 programs (3,000 tests): `python3 .../run.py 1 1500 2`
(several minutes).

**3. Look at the results.** The folder now contains:

- `results.json`: one entry per test with the inputs, gcc's result, the lines gcov
  executed (`executed`), the lines the proof covers (`proof`), whether the proof is a
  trace or non-trace proof, and every line only one side reports with its reason
  (`proof_only`, `gcov_only`);
- `work/<seed>_<k>/`: the files of each test: the program `f.c`, the gcc driver
  `driver.c`, the CBMC harness `harness.c` and CBMC's output `cbmc.out`.

To see one test around one line (gcov's counts, and the proof constraints on that line):

```console
python3 <source folder>/testing/generator/show.py 1_0 18
```

`1_0` is seed 1, input 0 (the `work/` folder name); `18` is the line; an optional third
argument sets how many lines around it to show (default 4). Run it from the folder that
holds `work/`.

## What a test does

1. Compile `f.c` with `gcc -O0 --coverage`, call `f` with the inputs, and read the
   executed lines with `gcov`.
2. Check with CBMC (`--unwind 6 --unwinding-assertions`) that the loop bound is large
   enough.
3. Run CBMC with `--proof-coverage` on a harness that asserts `f(inputs) == <gcc's
   result>`, and compare the proof's lines with gcov's.

Status values: `pass` (both sides gave a result; this says nothing about whether they
agree), `timeout` (CBMC took more than 60 s), `CBMC cannot prove gcc's result`,
`unwind bound too small`, `build failed`, `native run failed`.

## Kinds of difference

Lines the proof covers but gcov did not execute (`proof_only`):

| kind | meaning |
|---|---|
| `case label` | CBMC credits a `switch`'s comparisons to the `case` labels, gcc to the `switch` line |
| `no gcc code` | gcc decided the condition at compile time and emitted no code for the line (e.g. `b < b`); CBMC keeps it as a branch |
| `non-trace` | the proof uses a branch side or statement the run did not take |
| `statement line` / `multi-line statement` | a statement over two lines, credited to a different line by gcc and CBMC |
| `branch merge` / `condition attribution` | a merge of paths credited to a line that did not run (seen only in non-trace proofs) |

Lines gcov executed but the proof does not need (`gcov_only`): `entry/exit` (function
header and braces), `not needed` (ran, but the result does not depend on it),
`jump/label` (`break`, `continue`, `goto`, labels), `switch line`, `case label`,
`call statement`.
