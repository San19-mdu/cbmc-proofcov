/*******************************************************************\

Module: Proof Coverage

Author: proofcov

\*******************************************************************/

/// \file
/// Proof-based coverage: which statements are needed to prove the
/// assertions of a program?
///
/// The program is symbolically executed as for BMC. Assignments and phi
/// functions in user code become *soft* constraints,
/// each guarded by an activation literal; everything else (declarations,
/// assumptions, the negated assertions, library code) stays *hard*.
/// A minimal unsatisfiable subset of the soft constraints is a minimal
/// proof, and the lines it uses are the covered lines.

#ifndef CPROVER_GOTO_CHECKER_PROOF_COVERAGE_CHECKER_H
#define CPROVER_GOTO_CHECKER_PROOF_COVERAGE_CHECKER_H

#include "multi_path_symex_only_checker.h"
#include "proof_coverage_report.h"

#include <map>

#define OPT_PROOF_COVERAGE                                                     \
  "(proof-coverage)(proof-coverage-cores)(proof-coverage-max-cores):"          \
  "(proof-coverage-branch)(proof-coverage-track-undef)"                        \
  "(proof-coverage-experiment)(proof-coverage-annotate)(proof-coverage-verify)" \
  "(proof-coverage-mcdc):(proof-coverage-trace)"

#define HELP_PROOF_COVERAGE                                                    \
  " {y--proof-coverage} \t report the lines needed to prove the assertions\n"  \
  " {y--proof-coverage-cores} \t enumerate all minimal proofs (MARCO)\n"       \
  " {y--proof-coverage-max-cores} {un} \t stop after {un} proofs\n"            \
  " {y--proof-coverage-branch} \t also report branch coverage\n"               \
  " {y--proof-coverage-track-undef} \t allow phi functions to be bypassed "    \
  "when their inputs are undefined\n"                                          \
  " {y--proof-coverage-annotate} \t print the source annotated with "          \
  "coverage\n"                                                                 \
  " {y--proof-coverage-experiment} \t print the covered lines of each proof "  \
  "as 'COVERED: ...'\n"                                                        \
  " {y--proof-coverage-verify} \t re-check that every reported proof is "     \
  "minimal\n"                                                                 \
  " {y--proof-coverage-trace} \t tag each proof as trace (it only uses "   \
  "what the run executed) or non-trace\n"                                    \
  " {y--proof-coverage-mcdc} {umode} \t condition independence (MC/DC) of the " \
  "decisions evaluated by the test; {umode} is decision (masking MC/DC) or "    \
  "result (influence on the assertion)\n"

class cmdlinet;
class optionst;
class solver_factoryt;
class stack_decision_proceduret;

void parse_proof_coverage_options(const cmdlinet &, optionst &);

class proof_coverage_checkert : public multi_path_symex_only_checkert
{
public:
  proof_coverage_checkert(
    const optionst &options,
    ui_message_handlert &ui_message_handler,
    abstract_goto_modelt &goto_model);

  /// Runs the analysis and reports coverage.
  /// \return a CPROVER exit code
  int run();

protected:
  struct soft_constraintt
  {
    exprt constraint;
    proof_coverage_linest lines;
    std::string name;
    /// For a side of a phi: the phi's condition, and the value of it this
    /// side relies on (true for the if side, false for the else side).
    /// Nil otherwise.
    exprt merge_condition;
    bool merge_value = false;
    /// When the run executes what this constraint stands for: the path
    /// condition of a statement, the condition of a phi side
    /// (--proof-coverage-trace). Nil if unknown.
    exprt executed;
  };

  std::vector<soft_constraintt> soft_constraints;
  proof_coverage_programt program;

  /// Guard symbols introduced by goto-symex for branching conditions
  struct guardt
  {
    proof_coverage_linet line;
    exprt condition;
    /// Position in the SSA: when two guards occur in one path condition,
    /// the later one is nested inside the branch of the earlier one
    std::size_t order;
  };
  std::map<irep_idt, guardt> guards;

  /// Boolean variables other than guards that occur in phi conditions
  /// (e.g. the temporary of a condition with a function call), with the
  /// line that assigns them
  std::map<irep_idt, proof_coverage_linet> condition_symbol_lines;
  /// Their position among the guards: 2n when n guards precede the
  /// assignment (a guard with order g is at 2g + 1)
  std::map<irep_idt, std::size_t> condition_symbol_order;

  /// Branch (line of the condition, polarity) -> index into
  /// `program.branches`. Keyed by line, so the guards of all unwindings of
  /// a loop body, and of all operands of && and ||, form one branch.
  std::map<std::pair<proof_coverage_linet, bool>, std::size_t> branch_index;

  std::size_t selector_counter = 0;

  /// Ignores all assertions that are not user assertions.
  /// \return the number of remaining assertions
  std::size_t select_assertions();

  /// Turns the assignments in user code into soft constraints and marks
  /// the corresponding SSA steps as converted.
  void collect_soft_constraints();

  void add_soft_constraint(
    exprt constraint,
    proof_coverage_linest lines,
    std::string name,
    exprt merge_condition = nil_exprt(),
    bool merge_value = false,
    exprt executed = nil_exprt());

  /// Value of each guard symbol in the run of the program
  using guard_valuest = std::map<irep_idt, bool>;

  /// The guard values of the run: the program with all soft constraints
  /// and without the assertions, solved on its own from \p steps (the
  /// SSA steps before conversion).
  /// \return nothing if the values are not unique (nondeterministic
  ///   inputs) or the program has no run within the bounds
  std::optional<guard_valuest> run_guard_values(
    symex_target_equationt::SSA_stepst steps,
    solver_factoryt &solver_factory);

  /// The value of \p expr, a formula over guard symbols, in \p run
  std::optional<bool>
  evaluate(const exprt &expr, const guard_valuest &run) const;

  /// Adds to \p lines the lines of the branch conditions that give
  /// \p expr the \p value it has in \p run: of a conjunction that is
  /// false only the first false conjunct counts, the later ones were not
  /// evaluated.
  /// \return false if \p expr is not a formula over guard symbols
  bool deciding_lines(
    const exprt &expr,
    bool value,
    const guard_valuest &run,
    proof_coverage_linest &lines) const;

  /// The position of the first guard in \p expr (see `guardt::order`)
  std::size_t first_guard(const exprt &expr) const;

  void add_phi(const SSA_stept &step, const proof_coverage_linet &line);

  /// Records \p line, if given, in the branches that \p path_condition
  /// lies in, and the nesting of the branch conditions it mentions.
  void record_branch_membership(
    const exprt &path_condition,
    const std::optional<proof_coverage_linet> &line);

  /// The lines in the branch of the condition at \p decision
  proof_coverage_linest &
  branch_lines(const proof_coverage_linet &decision, bool polarity);

  /// The lines that can be covered, from the goto program
  void collect_coverable_lines();

  /// The flag stating that \p expr is undefined (track-undef mode)
  static exprt undef(const exprt &expr);

  /// The variables whose undefinedness lets a phi with condition
  /// \p condition be bypassed (track-undef mode)
  exprt any_undef(const exprt &condition) const;

  int report_failure(stack_decision_proceduret &);

  /// Converts \p steps (the SSA steps before conversion) into
  /// \p decision_procedure, without the assertions unless
  /// \p with_assertions, and adds the soft constraints as hard ones.
  void convert_program(
    symex_target_equationt::SSA_stepst steps,
    bool with_assertions,
    decision_proceduret &decision_procedure,
    bool add_soft_constraints = true);

  /// A condition of a decision: a non-constant leaf of its Boolean
  /// expression, replaced by `symbol`, which `definition` sets to the leaf
  struct mcdc_conditiont
  {
    symbol_exprt symbol;
    exprt definition;
    std::string text;
  };

  /// One evaluation of a decision: a branch condition, a Boolean
  /// expression with && or || in an assignment or return, or the
  /// condition of ?: (per unwinding and inlined call)
  struct mcdc_evaluationt
  {
    /// file:line#n, the n-th decision on the line (stable across tests)
    std::string key;
    std::string text;
    /// The condition under which the decision is evaluated
    exprt reached;
    /// The decision over the condition symbols
    exprt decision;
    std::vector<mcdc_conditiont> conditions;
  };

  /// Replaces the conditions of the decisions in \p steps by symbols and
  /// records the evaluations (--proof-coverage-mcdc)
  std::vector<mcdc_evaluationt>
  collect_mcdc(symex_target_equationt::SSA_stepst &steps);

  /// Number of decisions an instruction contains (see mcdc_evaluationt)
  std::size_t decisions_in(const goto_programt::instructiont &) const;

  /// For each soft constraint, whether the run executes what it stands for
  /// (--proof-coverage-trace); nothing if the test has more than one run
  std::optional<std::vector<std::optional<bool>>>
  run_executed(symex_target_equationt::SSA_stepst steps, solver_factoryt &);

  /// Reports which conditions independently affect their decision
  /// (\p mode decision) or the assertion (\p mode result) in the run
  void report_mcdc(
    const std::string &mode,
    const symex_target_equationt::SSA_stepst &steps,
    const std::vector<mcdc_evaluationt> &evaluations,
    solver_factoryt &solver_factory);
};

#endif // CPROVER_GOTO_CHECKER_PROOF_COVERAGE_CHECKER_H
