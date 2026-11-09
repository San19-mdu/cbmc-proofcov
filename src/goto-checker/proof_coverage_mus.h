/*******************************************************************\

Module: Proof Coverage - Minimal Unsatisfiable Subsets

Author: proofcov

\*******************************************************************/

/// \file
/// Minimal unsatisfiable subset (MUS) extraction and MARCO enumeration
/// over soft constraints guarded by activation literals.

#ifndef CPROVER_GOTO_CHECKER_PROOF_COVERAGE_MUS_H
#define CPROVER_GOTO_CHECKER_PROOF_COVERAGE_MUS_H

#include <util/expr.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

class conflict_providert;
class message_handlert;
class stack_decision_proceduret;

/// A set of soft constraint indices.
using proof_coverage_seedt = std::vector<std::size_t>;

/// Checks subsets of soft constraints for satisfiability.
/// Soft constraint i is enforced when its activation handle is assumed;
/// a soft constraint that is not assumed is free to be disabled.
class proof_coverage_subset_solvert
{
public:
  proof_coverage_subset_solvert(
    stack_decision_proceduret &solver,
    const conflict_providert &conflict_provider,
    std::vector<exprt> activation_handles);

  std::size_t size() const
  {
    return activation_handles.size();
  }

  /// \return true iff the hard constraints together with the soft
  ///   constraints in \p seed are satisfiable. If unsatisfiable, the
  ///   indices of \p seed in the final conflict are stored in `last_core`.
  bool check_subset(const proof_coverage_seedt &seed);

  /// The core of the most recent unsatisfiable `check_subset` call.
  const proof_coverage_seedt &last_core() const
  {
    return core;
  }

  /// Deletion-based minimisation of the unsatisfiable \p seed,
  /// refined with the solver's conflict after each unsatisfiable check.
  /// \p seed is taken by value: callers pass `last_core()`, which every
  /// check inside shrink overwrites.
  proof_coverage_seedt shrink(proof_coverage_seedt seed);

  /// Extends the satisfiable \p seed to a maximal satisfiable subset.
  proof_coverage_seedt grow(const proof_coverage_seedt &seed);

protected:
  stack_decision_proceduret &solver;
  const conflict_providert &conflict_provider;
  std::vector<exprt> activation_handles;
  proof_coverage_seedt core;
};

/// Enumerates minimal unsatisfiable subsets with the MARCO algorithm
/// (Liffiton et al.), using a separate SAT instance as the map solver.
class proof_coverage_marcot
{
public:
  proof_coverage_marcot(
    proof_coverage_subset_solvert &subset_solver,
    message_handlert &message_handler);

  ~proof_coverage_marcot();

  /// \return the next MUS, or an empty optional once all have been found
  std::optional<proof_coverage_seedt> next_mus();

protected:
  class map_solvert;

  proof_coverage_subset_solvert &subset_solver;
  std::unique_ptr<map_solvert> map_solver;
  bool done = false;
};

#endif // CPROVER_GOTO_CHECKER_PROOF_COVERAGE_MUS_H
