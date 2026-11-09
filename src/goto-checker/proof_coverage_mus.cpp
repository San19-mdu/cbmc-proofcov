/*******************************************************************\

Module: Proof Coverage - Minimal Unsatisfiable Subsets

Author: proofcov

\*******************************************************************/

/// \file
/// Minimal unsatisfiable subset (MUS) extraction and MARCO enumeration

#include "proof_coverage_mus.h"

#include <util/invariant.h>

#include <solvers/conflict_provider.h>
#include <solvers/sat/satcheck.h>
#include <solvers/stack_decision_procedure.h>

#include <algorithm>
#include <set>

proof_coverage_subset_solvert::proof_coverage_subset_solvert(
  stack_decision_proceduret &solver,
  const conflict_providert &conflict_provider,
  std::vector<exprt> activation_handles)
  : solver(solver),
    conflict_provider(conflict_provider),
    activation_handles(std::move(activation_handles))
{
}

bool proof_coverage_subset_solvert::check_subset(
  const proof_coverage_seedt &seed)
{
  std::vector<exprt> assumptions;
  assumptions.reserve(seed.size());
  for(const auto i : seed)
    assumptions.push_back(activation_handles[i]);

  solver.push(assumptions);
  const auto result = solver();

  core.clear();
  if(result == decision_proceduret::resultt::D_UNSATISFIABLE)
  {
    // the conflict must be read before the next solver call
    for(const auto i : seed)
    {
      if(conflict_provider.is_in_conflict(activation_handles[i]))
        core.push_back(i);
    }
  }

  solver.pop();

  INVARIANT(
    result != decision_proceduret::resultt::D_ERROR,
    "proof coverage requires a complete solver");

  return result == decision_proceduret::resultt::D_SATISFIABLE;
}

proof_coverage_seedt
proof_coverage_subset_solvert::shrink(proof_coverage_seedt seed)
{
  std::set<std::size_t> current(seed.begin(), seed.end());

  for(const auto i : seed)
  {
    if(current.find(i) == current.end())
      continue;

    current.erase(i);
    if(check_subset(proof_coverage_seedt(current.begin(), current.end())))
    {
      // i is necessary
      current.insert(i);
    }
    else
    {
      // still unsatisfiable without i: continue from the (smaller) core
      current = std::set<std::size_t>(core.begin(), core.end());
    }
  }

  return proof_coverage_seedt(current.begin(), current.end());
}

proof_coverage_seedt
proof_coverage_subset_solvert::grow(const proof_coverage_seedt &seed)
{
  proof_coverage_seedt result = seed;
  std::set<std::size_t> in_seed(seed.begin(), seed.end());

  for(std::size_t i = 0; i < size(); ++i)
  {
    if(in_seed.find(i) != in_seed.end())
      continue;

    result.push_back(i);
    if(!check_subset(result))
      result.pop_back();
  }

  return result;
}

/// The map solver keeps track of the subsets that are still unexplored:
/// one Boolean variable per soft constraint, blocking clauses for the
/// supersets of every MUS and the subsets of every MSS found so far.
class proof_coverage_marcot::map_solvert
{
public:
  map_solvert(std::size_t n, message_handlert &message_handler)
    : satcheck(message_handler)
  {
    variables.reserve(n);
    for(std::size_t i = 0; i < n; ++i)
    {
      variables.push_back(satcheck.new_variable());
      // High bias: decide unconstrained variables true, so seeds contain as
      // many soft constraints as possible and are usually unsatisfiable,
      // leading straight to a MUS. MiniSat's default (false) yields small,
      // satisfiable seeds that each cost a full grow() of n solver calls.
      // Note the inverted MiniSat convention: `false` selects the positive
      // literal.
      satcheck.set_polarity(variables.back(), false);
    }
  }

  /// \return an unexplored subset, preferring to include soft constraints
  std::optional<proof_coverage_seedt> next_seed()
  {
    if(satcheck.prop_solve() != propt::resultt::P_SATISFIABLE)
      return {};

    proof_coverage_seedt seed;
    for(std::size_t i = 0; i < variables.size(); ++i)
    {
      if(!satcheck.l_get(variables[i]).is_false())
        seed.push_back(i);
    }
    return seed;
  }

  /// Excludes \p mus and all its supersets.
  void block_up(const proof_coverage_seedt &mus)
  {
    bvt clause;
    for(const auto i : mus)
      clause.push_back(!variables[i]);
    satcheck.lcnf(clause);
  }

  /// Excludes \p mss and all its subsets.
  void block_down(const proof_coverage_seedt &mss)
  {
    std::set<std::size_t> in_mss(mss.begin(), mss.end());
    bvt clause;
    for(std::size_t i = 0; i < variables.size(); ++i)
    {
      if(in_mss.find(i) == in_mss.end())
        clause.push_back(variables[i]);
    }
    satcheck.lcnf(clause);
  }

protected:
  satcheck_no_simplifiert satcheck;
  bvt variables;
};

proof_coverage_marcot::proof_coverage_marcot(
  proof_coverage_subset_solvert &subset_solver,
  message_handlert &message_handler)
  : subset_solver(subset_solver),
    map_solver(
      std::make_unique<map_solvert>(subset_solver.size(), message_handler))
{
}

proof_coverage_marcot::~proof_coverage_marcot() = default;

std::optional<proof_coverage_seedt> proof_coverage_marcot::next_mus()
{
  while(!done)
  {
    const auto seed = map_solver->next_seed();
    if(!seed.has_value())
    {
      done = true;
      break;
    }

    if(subset_solver.check_subset(*seed))
    {
      const auto mss = subset_solver.grow(*seed);
      if(mss.size() == subset_solver.size())
      {
        // all soft constraints together are satisfiable: no MUS exists
        done = true;
        break;
      }
      map_solver->block_down(mss);
    }
    else
    {
      const auto mus = subset_solver.shrink(subset_solver.last_core());
      if(mus.empty())
      {
        // the hard constraints alone are unsatisfiable
        done = true;
      }
      else
        map_solver->block_up(mus);
      return mus;
    }
  }

  return {};
}
