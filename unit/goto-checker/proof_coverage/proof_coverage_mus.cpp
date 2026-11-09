/*******************************************************************\

Module: Unit tests for proof coverage MUS extraction and enumeration

Author: proofcov

\*******************************************************************/

/// \file
/// Tests proof_coverage_subset_solvert::shrink and proof_coverage_marcot
/// against a scripted solver whose unsatisfiable subsets are known.

#include <util/message.h>
#include <util/std_expr.h>

#include <goto-checker/proof_coverage_mus.h>
#include <solvers/conflict_provider.h>
#include <solvers/stack_decision_procedure.h>
#include <testing-utils/use_catch.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

/// A solver over activation symbols a0, a1, ...: the assumed set is
/// unsatisfiable iff it contains one of the given MUSes. Its conflict is the
/// whole assumed set -- a valid but non-minimal core, as real SAT solvers
/// return when several proofs overlap. That forces shrink() to refine.
class scripted_solvert : public stack_decision_proceduret,
                         public conflict_providert
{
public:
  explicit scripted_solvert(std::vector<std::set<std::size_t>> muses)
    : muses(std::move(muses))
  {
  }

  static exprt activation(std::size_t i)
  {
    return symbol_exprt("a" + std::to_string(i), bool_typet());
  }

  void push(const std::vector<exprt> &assumptions) override
  {
    assumed.clear();
    for(const auto &a : assumptions)
      assumed.insert(std::stoul(id2string(to_symbol_expr(a).get_identifier()).substr(1)));
  }
  void push() override
  {
  }
  void pop() override
  {
    assumed.clear();
  }

  bool is_in_conflict(const exprt &expr) const override
  {
    const auto i =
      std::stoul(id2string(to_symbol_expr(expr).get_identifier()).substr(1));
    return conflict.count(i) != 0;
  }

  void set_to(const exprt &, bool) override
  {
  }
  exprt handle(const exprt &expr) override
  {
    return expr;
  }
  exprt get(const exprt &) const override
  {
    return nil_exprt();
  }
  void print_assignment(std::ostream &) const override
  {
  }
  std::string decision_procedure_text() const override
  {
    return "scripted";
  }
  std::size_t get_number_of_solver_calls() const override
  {
    return calls;
  }

protected:
  resultt dec_solve(const exprt &) override
  {
    ++calls;
    for(const auto &mus : muses)
    {
      if(std::includes(
           assumed.begin(), assumed.end(), mus.begin(), mus.end()))
      {
        conflict = assumed;
        return resultt::D_UNSATISFIABLE;
      }
    }
    conflict.clear();
    return resultt::D_SATISFIABLE;
  }

  std::vector<std::set<std::size_t>> muses;
  std::set<std::size_t> assumed, conflict;
  std::size_t calls = 0;
};

static std::vector<exprt> activations(std::size_t n)
{
  std::vector<exprt> result;
  for(std::size_t i = 0; i < n; ++i)
    result.push_back(scripted_solvert::activation(i));
  return result;
}

static std::set<std::size_t> as_set(const proof_coverage_seedt &seed)
{
  return {seed.begin(), seed.end()};
}

TEST_CASE(
  "Proof coverage shrink returns a minimal core",
  "[core][goto-checker][proof_coverage]")
{
  const std::size_t n = 6;

  SECTION("Shrinking the solver's own last core")
  {
    // Regression test: shrink() used to take its seed by reference, and
    // callers pass last_core(), which every check inside shrink() rewrites.
    // Elements shifted into already-visited positions were never tested and
    // the result here was {1, 3, 4, 5} instead of {4, 5}.
    scripted_solvert solver({{4, 5}});
    proof_coverage_subset_solvert subset_solver(solver, solver, activations(n));

    proof_coverage_seedt all(n);
    for(std::size_t i = 0; i < n; ++i)
      all[i] = i;
    REQUIRE_FALSE(subset_solver.check_subset(all));
    REQUIRE(subset_solver.last_core().size() == n);

    const auto mus = subset_solver.shrink(subset_solver.last_core());
    REQUIRE(as_set(mus) == std::set<std::size_t>{4, 5});
  }

  SECTION("Every element of the result is necessary")
  {
    scripted_solvert solver({{0, 5}, {1, 2, 4}});
    proof_coverage_subset_solvert subset_solver(solver, solver, activations(n));

    proof_coverage_seedt all{0, 1, 2, 3, 4, 5};
    REQUIRE_FALSE(subset_solver.check_subset(all));
    const auto mus = subset_solver.shrink(subset_solver.last_core());

    REQUIRE_FALSE(subset_solver.check_subset(mus));
    for(std::size_t i = 0; i < mus.size(); ++i)
    {
      proof_coverage_seedt smaller = mus;
      smaller.erase(smaller.begin() + i);
      REQUIRE(subset_solver.check_subset(smaller));
    }
  }
}

TEST_CASE(
  "Proof coverage MARCO enumerates exactly the minimal cores",
  "[core][goto-checker][proof_coverage]")
{
  const std::vector<std::set<std::size_t>> expected{
    {0, 2}, {1, 3}, {2, 3, 4}, {5}};
  scripted_solvert solver(expected);
  proof_coverage_subset_solvert subset_solver(solver, solver, activations(8));
  null_message_handlert message_handler;
  proof_coverage_marcot marco(subset_solver, message_handler);

  std::set<std::set<std::size_t>> found;
  while(const auto mus = marco.next_mus())
  {
    // each core is reported once and is one of the minimal cores
    REQUIRE(found.insert(as_set(*mus)).second);
  }

  REQUIRE(found == std::set<std::set<std::size_t>>(expected.begin(), expected.end()));
}
