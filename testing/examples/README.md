# Examples

Small C programs for proof coverage. Run any of them from this folder with:

```console
pcbmc dilemma.c --proof-coverage --proof-coverage-annotate --verbosity 4
```

(`pcbmc` is the proof-coverage binary, see `README-RUN.md` step 5; or use the path
`../../build/bin/cbmc`.) Add `--proof-coverage-cores` to list every minimal proof, and
`--proof-coverage-trace` to see whether each proof only uses what the run executed.

Programs without `#include <assert.h>` print the warning
`function 'assert' is not declared`, which can be ignored.

## The examples

All pass (`Test pass: the assertions hold`) with the command above, except where noted.

| file | what it shows |
|---|---|
| `simple_dilemma.c` | the smallest case: an `if` that cannot change the result |
| `dilemma.c` | two `if`s; the second gives `r = 1` either way, so there are two proofs (one needs the first `if`, one does not) |
| `non_trace.c`, `paper_non_trace.c` | both branches satisfy the assert (`b > 0`), so besides the proof along the run there is a *non-trace* proof using the branch that did not run (`--proof-coverage-cores --proof-coverage-trace`) |
| `skippable_if.c` | both branches assign the same value. The default proof follows the run (`a = 5`, condition false, `else`); `--proof-coverage-cores` also finds a non-trace proof that uses both branches instead of `a`'s value |
| `must_skip_if.c` | the proof needs that the inner `if` was *not* taken |
| `nested_ifs.c` | nested `if`s; the assert is about `b`, so `a` is only needed to decide the branches: `a = a / 3` and the final `a = a + b` are not needed |
| `bank.c` | a balance calculation with an approval branch; `approved` is never needed |
| `modulo.c` | `%` and `/` in a branch decision |
| `empty_false.c` | a constant condition (`1 > 0`) and a block that does nothing |
| `short_circuit.c` | `\|\|` where the first operand already decides the condition |
| `tree_node_test.c` | `(a && b) \|\| c` in one condition |
| `or_in_assert.c` | `\|\|` in the assert itself: only one operand is needed |
| `three_cases.c` | three `if`s, only the third sets the result |
| `speed_paper.c` | nested branches where most statements change `temp`, but the assert is about `speed` |
| `zero.c` | `cost = qty * price` with `price = 0`: `qty` is not needed |
| `if_in_for.c` | an `if` inside a loop: **needs a loop bound**, `--unwind 11` (the loop runs 10 times) |
| `vowels.c` | **fails on purpose** (`Test fail`): it asserts that one of three uninitialised values is 12, so there is no proof and no coverage. Needs `--unwind 6`; add `--trace` for the counterexample |
| `tcas.c`, `tcas_original.c` | **not runnable as is**: the TCAS collision-avoidance program, which reads its 12 inputs from the command line (`atoi(argv[…])`) and has no assert. It is the source for the TCAS experiment, which replaces the inputs with constants and adds an assert per test vector |

`external/` holds four programs from the SemSlice evaluation data set (see
`external/README.md` for the citation):

| file | what it shows |
|---|---|
| `conflicting_single_line_removes.c` | an `\|\|` condition where either operand alone gives the result |
| `dead_code_after_ssa.c` | assignments that are overwritten before use (`x = 2; x = x; x = 1;`) |
| `dead_code_unused_variable.c` | a variable (`y`) that is set but never used |
| `identity_not_modifying.c` | assignments that do not change anything (`p = p`, `x = y` with equal values) |

## Run them all

```console
for f in *.c external/*.c; do
  echo "== $f"
  pcbmc "$f" --proof-coverage --unwind 11 --verbosity 4 | grep -E "Test|Line coverage"
done
```

`--unwind 11` is harmless for programs without loops. `tcas.c` and `tcas_original.c` do
not finish this way (see above); skip them or stop them with Ctrl+C.
