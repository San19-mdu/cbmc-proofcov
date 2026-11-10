# Running proof coverage in CBMC

This CBMC fork (branch `proofcov`, based on `cbmc-6.11.0`) adds **proof coverage**:
which lines of a C program are needed to *prove* its assertions. It is the CBMC
version of the Python `proofcov.py` tool and uses only CBMC's own front end,
symbolic execution and SAT solver (no z3).

Steps 1–6 take you from nothing to a first result. The build (step 3) takes a few
minutes (about 3 with 8 cores, longer on fewer); everything else is quick.

## Step 1: install the build tools

On **Ubuntu / Debian / WSL**:

```console
sudo apt update
sudo apt install g++ gcc flex bison make git curl patch cmake unzip
```

On **Fedora / Red Hat**:

```console
sudo dnf install gcc gcc-c++ flex bison make git curl patch cmake unzip
```

On **macOS** (with [Homebrew](https://brew.sh) and the Xcode command line tools):

```console
xcode-select --install
brew install cmake flex bison
```

You need CMake 3.8 or newer and a C++17 compiler (gcc 7+ or clang 5+). Check with
`cmake --version` and `g++ --version`.

## Step 2: get the code

Pick **one** of these. Both give you a folder with the source in it.

**A. With git** (recommended: easy to update later):

```console
cd ~
git clone --branch proofcov https://github.com/San19-mdu/cbmc-proofcov.git
cd cbmc-proofcov
```

**B. As a ZIP file** (no git needed):

```console
cd ~
curl -L -o cbmc-proofcov.zip https://github.com/San19-mdu/cbmc-proofcov/archive/refs/heads/proofcov.zip
unzip cbmc-proofcov.zip
cd cbmc-proofcov-proofcov
```

(Or on the GitHub page: switch the branch to `proofcov`, then **Code → Download ZIP**,
and unzip it.) The ZIP unpacks into `cbmc-proofcov-proofcov`, the git clone into
`cbmc-proofcov`. The rest of this guide calls this folder the **source folder**.

## Step 3: build

From the source folder:

```console
cmake -S . -B build -DWITH_JBMC=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4 --target cbmc
```

- The first command needs an internet connection: it downloads the MiniSat SAT
  solver (that is what `curl` and `patch` are for).
- `-j4` builds with 4 parallel jobs. Use your number of CPU cores (`nproc`) for a
  faster build, or a lower number if the computer runs out of memory.
- `-DWITH_JBMC=OFF` skips the Java verifier, which proof coverage does not need.

The result is one program: `build/bin/cbmc` inside the source folder.

## Step 4: check that it works

```console
build/bin/cbmc --version
build/bin/cbmc --help | grep proof-coverage
```

The first command prints `6.11.0` followed by the commit in brackets, e.g.
`6.11.0 (11cfbc9a)` (a ZIP build has no git information and prints `6.11.0 (n/a)`).
The second prints the `--proof-coverage` options. If it prints nothing, you are
running a CBMC without proof coverage.

## Step 5: make it easy to run ("install")

The build does not need to be installed anywhere. To run it from any folder under
a short name, link it into your personal `bin` folder as **`pcbmc`**:

```console
mkdir -p ~/.local/bin
ln -sf "$PWD/build/bin/cbmc" ~/.local/bin/pcbmc
pcbmc --version
```

If `pcbmc` is not found, add `~/.local/bin` to your PATH (once, then open a new
terminal):

```console
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
```

Use the name `pcbmc`, not `cbmc`: if CBMC is also installed through your package
manager, `cbmc` is that version, which has no proof coverage and fails with
`Unknown option: --proof-coverage`. Avoid `sudo cmake --install build` for the
same reason: it would install over (or next to) the system `cbmc`.

## Step 6: first run

Save this program as `dilemma.c` (any folder):

```c
#include <assert.h>

int main(void)
{
  int a = 1;
  int b;
  int r;

  if (a == 1)
    b = 1;
  else
    b = 0;

  if (b == 1)
    r = 1;
  else
    r = 1;

  assert(r == 1);
  return 0;
}
```

Run:

```console
pcbmc dilemma.c --proof-coverage --proof-coverage-annotate --verbosity 4
```

Output:

```
Test pass: the assertions hold

** Proof core 1 (5 soft constraints)
Core: line5.0 line10.0 phi.if.9.main::1::b!0@1#4 line15.0 phi.if.14.main::1::r!0@1#4
File dilemma.c:
001   #include <assert.h>
002
003   int main(void)
004   {
005 +   int a = 1;
006     int b;
007     int r;
008
009 +   if (a == 1)
010 +     b = 1;
011     else
012 -     b = 0;
013
014 +   if (b == 1)
015 +     r = 1;
016     else
017 -     r = 1;
018
019 +   assert(r == 1);
020     return 0;
021   }
Covered lines: dilemma.c:5 dilemma.c:9 dilemma.c:10 dilemma.c:14 dilemma.c:15 dilemma.c:19
Line coverage: 6/8 (75.00%)
```

Done. The rest of this file explains the output and the options.

## Reading the output

| Mark | Meaning |
|---|---|
| `+` | covered: the proof needs this line |
| `-` | a statement the proof does **not** need |
| (blank) | declaration, brace, comment: not counted |

- **`Test pass: the assertions hold`**: the proof succeeded; the coverage follows.
- **`Test fail: an assertion can be violated`**: there is no proof, so there is no
  coverage. Add `--trace` to see a counterexample. Exit code 10.
- **`Core:`** lists the constraints in the proof (`lineN.k` = a statement on line N,
  `phi.if/else.N.var` = the merge of variable `var` after the `if` on line N).
- A proof is *minimal*, not necessarily the *smallest*: other proofs may exist.
  `--proof-coverage-cores` lists all of them. `dilemma.c` has two:

  ```console
  pcbmc dilemma.c --proof-coverage --proof-coverage-cores --verbosity 4
  ```
  The second proof (lines 14, 15, 17, 19) does not need `a` or `b` at all: `r` is 1
  whichever way the second `if` goes.

The program needs at least one `assert` (or `__CPROVER_assert`): proof coverage
answers "which lines does the proof of the assertions need?".

## Options

| Option | Effect |
|---|---|
| `--proof-coverage` | required: report the lines needed to prove the assertions (one minimal proof) |
| `--proof-coverage-annotate` | print the source with `+` / `-` marks and the core's constraints |
| `--proof-coverage-branch` | also report branch coverage (with `-annotate`: per branch) |
| `--proof-coverage-cores` | enumerate **all** minimal proofs (MARCO); one report per proof |
| `--proof-coverage-max-cores N` | stop after N proofs (implies `-cores`) |
| `--proof-coverage-trace` | tag each proof as trace (it only uses what the run executed) or non-trace |
| `--proof-coverage-track-undef` | let a phi be bypassed when its inputs are undefined (as `--track-undef` in proofcov.py) |
| `--proof-coverage-mcdc decision\|result` | condition independence (MC/DC) of the decisions evaluated by the test |
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

Proof coverage only proves the program's own `assert`s. CBMC's automatic checks
(array bounds, overflow, division by zero, …) are not part of the proof and are not
reported in this mode. To look for such bugs, run the same file once without
`--proof-coverage` (e.g. `pcbmc file.c --bounds-check --signed-overflow-check`).

## More examples

The folder [`testing/`](testing/README.md) has more: [`testing/examples/`](testing/examples/README.md)
holds about 20 small programs, each showing one behaviour of proof coverage, and
[`testing/generator/`](testing/generator/README.md) a random program generator that
compares gcc's line coverage with proof coverage.

A loop: 10 iterations, so `--unwind 11`. Save as `loop.c`:

```c
#include <assert.h>

int main(void)
{
  int sum = 0;
  for (int i = 0; i < 10; i++)
    sum = sum + i;
  assert(sum == 45);
  return 0;
}
```

```console
pcbmc loop.c --proof-coverage --unwind 11 --verbosity 4
```

An assertion that can fail (no coverage; `--trace` shows how). Save as `failing.c`:

```c
#include <assert.h>

int nondet_int(void);   /* CBMC: any int */

int main(void)
{
  int x = nondet_int();
  int y = x * 2;
  assert(y > x);        /* false for x <= 0 */
  return 0;
}
```

```console
pcbmc failing.c --proof-coverage --trace --verbosity 4
```

Other useful runs:

```console
# branch coverage, per branch
pcbmc dilemma.c --proof-coverage --proof-coverage-branch --proof-coverage-annotate --verbosity 4

# same output format as proofcov.py --experiment (one line per proof)
pcbmc dilemma.c --proof-coverage --proof-coverage-cores --proof-coverage-experiment --verbosity 4

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
| `python3 proofcov.py F --experiment` | `pcbmc F --proof-coverage --proof-coverage-experiment` |
| `... --enumerate-cores` | `... --proof-coverage-cores` |
| `... --line --branch` | `pcbmc F --proof-coverage --proof-coverage-branch` |
| `... --track-undef` | `... --proof-coverage-track-undef` |

Differences to expect:

- **Line totals:** CBMC also counts initialised declarations as coverable,
  so `Line coverage: a/b` totals differ from Python's; compare covered lines instead.
- **Single proof:** the tools may return different, equally valid minimal proofs.
  With all proofs listed (`--enumerate-cores` / `--proof-coverage-cores`) they agree
  on the programs both can run.
- **Integer semantics:** CBMC models C's 32-bit `int` (with overflow); Python uses
  unbounded integers, so it can find proofs that are not valid for real C.
- **C support:** CBMC handles full C (loops, `return`, function calls, `switch`,
  `goto`, pointers, the C library); the Python tool only a small subset.

## Updating

If you used git (step 2A), from the source folder:

```console
git pull
cmake --build build -j4 --target cbmc
```

With a ZIP, download and unzip it again, then repeat step 3. The `pcbmc` link from
step 5 keeps working as long as the source folder stays in the same place.

## Tests

From the source folder:

```console
# end-to-end tests of --proof-coverage
cd regression/cbmc-proof-coverage
../test.pl -e -p -c "$(cd ../.. && pwd)/build/bin/cbmc"
cd ../..

# unit tests of core extraction / enumeration (builds the unit-test program first)
cmake --build build -j4 --target unit
build/bin/unit "[proof_coverage]"
```

## Troubleshooting

| Problem | Fix |
|---|---|
| `Unknown option: --proof-coverage` | you are running the system `cbmc`; use `pcbmc` or `build/bin/cbmc` |
| `pcbmc: command not found` | add `~/.local/bin` to your PATH (step 5) and open a new terminal |
| CMake fails while downloading MiniSat | check the internet connection and that `curl` and `patch` are installed, then re-run step 3 |
| the build stops with `Killed` or runs out of memory | build with fewer jobs: `cmake --build build -j2 --target cbmc` |
| a program with a loop never finishes | add `--unwind N` with N above the number of iterations |
| `proof coverage requires at least one assertion` | add an `assert(...)` to the program |
