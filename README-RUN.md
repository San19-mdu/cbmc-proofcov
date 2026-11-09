# Running proof coverage in CBMC

This CBMC fork (branch `proofcov`, based on `cbmc-6.11.0`) adds **proof coverage**:
which lines of a C program are needed to *prove* its assertions. It is the CBMC
version of the Python tool in [`../proofcov`](../proofcov) and uses only CBMC's own
front end, symbolic execution and SAT solver (no z3).

## Two important things before getting started

1. **Use the binary you built, not `cbmc` on your PATH.** `cbmc` on the PATH is the
   system install (`/usr/bin/cbmc`), which has no proof coverage and fails with
   `Unknown option: --proof-coverage`. The proof-coverage binary is:

   ```console
   ~/proofcov/cbmc/build/bin/cbmc
   ```

2. **Pass `--proof-coverage`.** Without it the binary does ordinary CBMC verification
   and only prints `VERIFICATION SUCCESSFUL` / `FAILED`, with no coverage.

Directory layout (note the two `proofcov` levels: the example programs are in
`~/proofcov/proofcov/examples`, not `~/proofcov/examples`):

```
~/proofcov/
├── proofcov/          the proofcov repo: examples/, proofcov/proofcov.py, tcas/, tests/
│   └── examples/dilemma.c
└── cbmc/              this CBMC fork
    └── build/bin/cbmc the proof-coverage binary
```

To save typing, define an alias:

```console
alias pcbmc=~/proofcov/cbmc/build/bin/cbmc
```

(Equivalently, `cd ~/proofcov/cbmc/build/bin` and use `./cbmc` in place of `pcbmc`.)

## Build

```console
cd ~/proofcov/cbmc
cmake -S . -B build -DWITH_JBMC=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target cbmc
```

Needs `g++ cmake flex bison make patch curl` (`sudo apt install ...`). After changing
the source, re-run the last command.

## Quick start

The program needs at least one `assert` (or `__CPROVER_assert`): proof coverage
answers "which lines does the proof of the assertions need?". From any directory:

```console
pcbmc ~/proofcov/proofcov/examples/dilemma.c --proof-coverage --proof-coverage-annotate --verbosity 4
```

Output (the `warning: function 'assert' is not declared` line can be ignored, or
add `#include <assert.h>`):

```
Test pass: the assertions hold

** Proof core 1 (5 soft constraints)
Core: line5.0 line8.0 phi.if.7.main::1::b!0@1#4 line14.0 phi.if.13.main::1::r!0@1#4
File /home/simon/proofcov/proofcov/examples/dilemma.c:
001   void main() {
002       int a;
003       int b;
004       int r;
005 +     a = 1;
006
007 +     if (a == 1) {
008 +         b = 1;
009       } else {
010 -         b = 0;
...
019 +     assert(r == 1);
020   }
Covered lines: /home/simon/proofcov/proofcov/examples/dilemma.c:5 ... dilemma.c:19
Line coverage: 6/8 (75.00%)
```

How to read it:

| Mark | Meaning |
|---|---|
| `+` (green) | covered: the proof needs this line |
| `-` (red) | coverable statement the proof does **not** need |
| (blank) | declaration, brace, comment: not counted |

- **`Test pass: the assertions hold`** means the proof succeeded; coverage follows.
- **`Test fail: an assertion can be violated`** means there is no proof, so there is
  no coverage. Add `--trace` to see a counterexample. Exit code 10.
- **`Core:`** lists the constraints in the proof (`lineN.k` = a statement on line N,
  `phi.if/else.N.var` = the merge of variable `var` after the `if` on line N).
- A proof is *minimal*, not necessarily the *smallest*: other proofs may exist.
  Use `--proof-coverage-cores` to list all of them.

## Options

| Option | Effect |
|---|---|
| `--proof-coverage` | required: report the lines needed to prove the assertions (one minimal proof) |
| `--proof-coverage-annotate` | print the source with `+` / `-` marks and the core's constraints |
| `--proof-coverage-branch` | also report branch coverage (with `-annotate`: per branch) |
| `--proof-coverage-cores` | enumerate **all** minimal proofs (MARCO); one report per proof |
| `--proof-coverage-max-cores N` | stop after N proofs (implies `-cores`) |
| `--proof-coverage-track-undef` | let a phi be bypassed when its inputs are undefined (as `--track-undef` in proofcov.py) |
| `--proof-coverage-experiment` | print only `COVERED: <lines>` per proof (format of proofcov.py `--experiment`) |
| `--proof-coverage-verify` | re-check that every reported proof is minimal (debugging) |

Ordinary CBMC options still apply, in particular:

| Option | Effect |
|---|---|
| `--verbosity 4` | hide CBMC's progress messages, keep the results |
| `--unwind N` | **required for loops**: unroll each loop N times. Without it, a program with a loop never finishes. If N is too small, the proof is vacuous and almost nothing is covered; pick N above the real iteration count |
| `--function f` | start from function `f` instead of `main` (e.g. a test harness) |
| `--trace` | counterexample when an assertion fails |
| `--json-ui` | machine-readable JSON output |

## More examples

```console
# branch coverage, per branch
pcbmc ~/proofcov/proofcov/examples/dilemma.c --proof-coverage --proof-coverage-branch --proof-coverage-annotate --verbosity 4

# all minimal proofs (dilemma.c has two)
pcbmc ~/proofcov/proofcov/examples/dilemma.c --proof-coverage --proof-coverage-cores --verbosity 4

# a program with a loop (10 iterations, so --unwind 11)
pcbmc ~/proofcov/proofcov/examples/if_in_for.c --proof-coverage --unwind 11 --proof-coverage-annotate --verbosity 4

# an assertion that can fail: no coverage, show the counterexample
pcbmc ~/proofcov/proofcov/examples/vowels.c --proof-coverage --unwind 6 --trace --verbosity 4

# same output format as proofcov.py --experiment (one line per proof)
pcbmc ~/proofcov/proofcov/examples/dilemma.c --proof-coverage --proof-coverage-cores --proof-coverage-experiment --verbosity 4

# one function of a larger program: write a harness that calls it and asserts the result
pcbmc program.c harness.c --function my_harness --proof-coverage --proof-coverage-annotate --verbosity 4
```

A harness for a single function looks like this (`harness.c`):

```c
#include <assert.h>
#include <stdbool.h>

bool isLeapYear(unsigned int);

void my_harness(void)
{
  assert(isLeapYear(2024) == true);   // concrete test case
}

void my_symbolic_harness(void)
{
  unsigned int year;                  // no value: any year
  bool result = isLeapYear(year);
  assert(year % 4 == 0 || result == false);
}
```

## Comparing with the Python tool

| proofcov.py | CBMC |
|---|---|
| `python3 ~/proofcov/proofcov/proofcov/proofcov.py F --experiment` | `pcbmc F --proof-coverage --proof-coverage-experiment` |
| `... --enumerate-cores` | `... --proof-coverage-cores` |
| `... --line --branch` | `pcbmc F --proof-coverage --proof-coverage-branch` |
| `... --track-undef` | `... --proof-coverage-track-undef` |

`tcas/experiment.py` uses CBMC with `PROOFCOV_BACKEND=cbmc` (and
`CBMC=~/proofcov/cbmc/build/bin/cbmc`, because `cbmc` on the PATH is the system one).

Differences to expect:

- **Line numbers / totals:** CBMC also counts initialised declarations as coverable,
  so `Line coverage: a/b` totals differ from Python's; compare covered lines instead.
- **Single core:** the tools may return different, equally valid minimal proofs.
- **Integer semantics:** CBMC models C's 32-bit `int` (with overflow); Python uses
  unbounded integers, so it can find proofs that are not valid for real C.
- **C support:** CBMC handles full C (loops, `return`, function calls, `float`,
  pointers, the C library); the Python tool only a small subset.

## Tests

```console
# end-to-end tests of --proof-coverage
cd ~/proofcov/cbmc/regression/cbmc-proof-coverage
../test.pl -e -p -c ~/proofcov/cbmc/build/bin/cbmc

# unit tests of core extraction / enumeration
cd ~/proofcov/cbmc
cmake --build build -j --target unit
build/bin/unit "[proof_coverage]"
```
