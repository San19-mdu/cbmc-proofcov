/*******************************************************************\

Module: Proof Coverage - Condition Independence (MC/DC)

Author: proofcov

\*******************************************************************/

/// \file
/// Proof-based MC/DC for one test (concrete inputs).
///
/// Every condition (non-constant leaf of the Boolean expression of a
/// decision) gets a symbol of its own, defined by a switchable constraint.
/// For each evaluation of a decision in the run, a condition is
/// - decision mode: independent if, with only its definition switched off,
///   the decision can have the other outcome (masking MC/DC);
/// - result mode: independent if, with only its definition switched off,
///   the assertion can fail, i.e. the definition is in every minimal proof.

#include "proof_coverage_checker.h"

#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include <langapi/language_util.h>

#include "solver_factory.h"

#include <functional>
#include <map>
#include <set>
#include <sstream>

/// The leaves of the Boolean structure (and, or, not) of \p expr
static void
collect_leaves(const exprt &expr, std::vector<const exprt *> &leaves)
{
  if(expr.id() == ID_and || expr.id() == ID_or || expr.id() == ID_not)
  {
    for(const auto &op : expr.operands())
      collect_leaves(op, leaves);
  }
  else
    leaves.push_back(&expr);
}

/// \p expr with its leaves replaced, in order, by \p symbols (a leaf
/// without a symbol stays)
static exprt replace_leaves(
  const exprt &expr,
  const std::vector<std::optional<symbol_exprt>> &symbols,
  std::size_t &next)
{
  if(expr.id() == ID_and || expr.id() == ID_or || expr.id() == ID_not)
  {
    exprt result = expr;
    for(auto &op : result.operands())
      op = replace_leaves(op, symbols, next);
    return result;
  }
  const auto &symbol = symbols[next++];
  return symbol.has_value() ? exprt(*symbol) : expr;
}

/// Evaluates \p expr as C does (left to right, short-circuit), with the
/// condition symbols' values in \p values, and marks in \p evaluated the
/// symbols that are evaluated
static bool evaluate_short_circuit(
  const exprt &expr,
  const std::map<irep_idt, bool> &values,
  std::set<irep_idt> &evaluated,
  const namespacet &ns)
{
  if(expr.id() == ID_not)
    return !evaluate_short_circuit(
      to_not_expr(expr).op(), values, evaluated, ns);
  if(expr.id() == ID_and || expr.id() == ID_or)
  {
    // && stops at the first false operand, || at the first true one
    const bool stop = expr.id() == ID_or;
    for(const auto &op : expr.operands())
    {
      if(evaluate_short_circuit(op, values, evaluated, ns) == stop)
        return stop;
    }
    return !stop;
  }
  if(expr.id() == ID_symbol)
  {
    const auto value = values.find(to_symbol_expr(expr).get_identifier());
    if(value != values.end())
    {
      evaluated.insert(value->first);
      return value->second;
    }
  }
  // a constant condition
  return simplify_expr(expr, ns).is_true();
}

/// \p expr with its leaves replaced, in order, by \p replacements
static exprt substitute_leaves(
  const exprt &expr,
  const std::vector<exprt> &replacements,
  std::size_t &next)
{
  if(expr.id() == ID_and || expr.id() == ID_or || expr.id() == ID_not)
  {
    exprt result = expr;
    for(auto &op : result.operands())
      op = substitute_leaves(op, replacements, next);
    return result;
  }
  return replacements[next++];
}

static bool has_and_or(const exprt &expr)
{
  if(expr.id() == ID_and || expr.id() == ID_or)
    return true;
  return expr.id() == ID_not && has_and_or(to_not_expr(expr).op());
}

/// A decision found in an expression, with the condition under which the
/// expression around it evaluates it (the arms of ?:)
struct found_decisiont
{
  const exprt *root;
  exprt context;
};

/// The decisions in \p expr, in pre-order: Boolean expressions with && or
/// || that are not part of a larger one, and the conditions of ?:
static void find_decisions(
  const exprt &expr,
  bool in_boolean,
  const exprt &context,
  std::vector<found_decisiont> &found)
{
  if(expr.id() == ID_and || expr.id() == ID_or || expr.id() == ID_not)
  {
    if(!in_boolean && has_and_or(expr))
      found.push_back({&expr, context});
    for(const auto &op : expr.operands())
      find_decisions(op, true, context, found);
    return;
  }
  if(expr.id() == ID_if)
  {
    const auto &if_expr = to_if_expr(expr);
    found.push_back({&if_expr.cond(), context});
    find_decisions(if_expr.cond(), true, context, found);
    find_decisions(
      if_expr.true_case(), false, and_exprt(context, if_expr.cond()), found);
    find_decisions(
      if_expr.false_case(),
      false,
      and_exprt(context, not_exprt(if_expr.cond())),
      found);
    return;
  }
  for(const auto &op : expr.operands())
    find_decisions(op, false, context, found);
}

static bool same_shape(const exprt &a, const exprt &b)
{
  if(a.id() != b.id() || a.operands().size() != b.operands().size())
    return false;
  for(std::size_t i = 0; i < a.operands().size(); ++i)
  {
    if(!same_shape(a.operands()[i], b.operands()[i]))
      return false;
  }
  return true;
}

/// The first subexpression of \p ssa (pre-order) with the shape of \p expr
/// (symbols renamed), or null
static const exprt *find_shape(const exprt &expr, const exprt &ssa)
{
  if(same_shape(expr, ssa))
    return &ssa;
  for(const auto &op : ssa.operands())
  {
    if(const auto found = find_shape(expr, op))
      return found;
  }
  return nullptr;
}

/// Whether all leaves of \p decision are constant (while (1))
static bool constant_decision(const exprt &decision, const namespacet &ns)
{
  std::vector<const exprt *> leaves;
  collect_leaves(decision, leaves);
  for(const auto leaf : leaves)
  {
    if(!simplify_expr(*leaf, ns).is_constant())
      return false;
  }
  return true;
}

/// The expression of an instruction that may contain decisions other than
/// a branch condition, or null
static const exprt *decision_expression(const goto_programt::instructiont &i)
{
  if(i.is_assign())
    return &i.assign_rhs();
  if(i.is_set_return_value())
    return &i.return_value();
  return nullptr;
}

/// The decision of a branch: the C condition, i.e. the jump condition
/// without the negation goto conversion adds for `if (d)`
static exprt branch_decision(const goto_programt::instructiont &i)
{
  const exprt &condition = i.condition();
  return condition.id() == ID_not ? to_not_expr(condition).op() : condition;
}

std::size_t proof_coverage_checkert::decisions_in(
  const goto_programt::instructiont &i) const
{
  // switch cases (marked with their case number) are not decisions
  if(i.is_goto())
  {
    return i.source_location().get_case_number().empty() &&
               !constant_decision(branch_decision(i), ns)
             ? 1
             : 0;
  }
  const exprt *expr = decision_expression(i);
  if(expr == nullptr)
    return 0;
  std::vector<found_decisiont> found;
  find_decisions(*expr, false, true_exprt(), found);
  std::size_t count = 0;
  for(const auto &d : found)
    count += constant_decision(*d.root, ns) ? 0 : 1;
  return count;
}

std::vector<proof_coverage_checkert::mcdc_evaluationt>
proof_coverage_checkert::collect_mcdc(symex_target_equationt::SSA_stepst &steps)
{
  // Decisions are numbered per source line, over the instructions of the
  // line in program order, so that a decision has the same name in every
  // test: file:line#n
  std::map<unsigned, std::size_t> before; // location number -> decisions
  for(const auto &function : goto_model.get_goto_functions().function_map)
  {
    std::map<std::size_t, std::size_t> on_line;
    for(const auto &i : function.second.body.instructions)
    {
      const auto line = proof_coverage_user_line(i.source_location());
      if(!line.has_value())
        continue;
      before[i.location_number] = on_line[line->line];
      on_line[line->line] += decisions_in(i);
    }
  }
  const auto key = [&](
                     const goto_programt::const_targett &pc,
                     const proof_coverage_linet &line,
                     std::size_t k) {
    return id2string(line.file) + ":" + std::to_string(line.line) + "#" +
           std::to_string(before[pc->location_number] + k + 1);
  };

  std::vector<mcdc_evaluationt> evaluations;
  std::size_t counter = 0;
  const auto new_condition = [&](const exprt &leaf, const std::string &text) {
    const symbol_exprt symbol(
      "proofcov::condition#" + std::to_string(counter++), bool_typet());
    return mcdc_conditiont{symbol, equal_exprt(symbol, leaf), text};
  };

  // symex records the GOTO of a decision, with the path condition, just
  // before the guard definition (whose own guard is true)
  const SSA_stept *last_goto = nullptr;

  for(auto &step : steps)
  {
    // the MC/DC solvers get the whole program from the steps
    step.converted = false;

    if(step.is_goto())
      last_goto = &step;

    if(!step.is_assignment() || step.ignore)
      continue;

    const auto &pc = step.source.pc;
    const auto line = proof_coverage_user_line(pc->source_location());
    if(!line.has_value())
      continue;
    const irep_idt &function = step.source.function_id;

    if(step.assignment_type == symex_targett::assignment_typet::GUARD)
    {
      if(
        !pc->is_goto() || last_goto == nullptr || last_goto->source.pc != pc ||
        decisions_in(*pc) == 0)
      {
        continue;
      }

      // The guard is the negated jump condition: either way its leaves are
      // those of the decision, in the same order.
      const exprt decision = branch_decision(*pc);
      std::vector<const exprt *> ssa_leaves, leaves;
      collect_leaves(step.ssa_rhs, ssa_leaves);
      collect_leaves(decision, leaves);
      if(ssa_leaves.size() != leaves.size())
        continue;

      mcdc_evaluationt evaluation{
        key(pc, *line, 0),
        from_expr(ns, function, decision),
        last_goto->guard,
        nil_exprt(),
        {}};
      std::vector<exprt> ssa_replacements, replacements;
      for(std::size_t i = 0; i < leaves.size(); ++i)
      {
        // constant conditions (while (1)) can not be independent
        if(simplify_expr(*leaves[i], ns).is_constant())
        {
          ssa_replacements.push_back(*ssa_leaves[i]);
          replacements.push_back(*leaves[i]);
          continue;
        }
        evaluation.conditions.push_back(
          new_condition(*ssa_leaves[i], from_expr(ns, function, *leaves[i])));
        ssa_replacements.push_back(evaluation.conditions.back().symbol);
        replacements.push_back(evaluation.conditions.back().symbol);
      }

      std::size_t next = 0;
      step.ssa_rhs = substitute_leaves(step.ssa_rhs, ssa_replacements, next);
      step.cond_expr = equal_exprt(step.ssa_lhs, step.ssa_rhs);
      next = 0;
      evaluation.decision = substitute_leaves(decision, replacements, next);
      evaluations.push_back(std::move(evaluation));
      continue;
    }

    // decisions in the expression of an assignment or return
    const exprt *expr = decision_expression(*pc);
    if(
      step.assignment_type != symex_targett::assignment_typet::STATE ||
      expr == nullptr || decisions_in(*pc) == 0)
    {
      continue;
    }
    const exprt *ssa_expr = find_shape(*expr, step.ssa_rhs);
    if(ssa_expr == nullptr)
      continue;
    std::vector<found_decisiont> found, ssa_found;
    find_decisions(*expr, false, true_exprt(), found);
    find_decisions(*ssa_expr, false, true_exprt(), ssa_found);
    if(found.size() != ssa_found.size())
      continue;

    // the decisions of this step, by their root in the SSA expression; the
    // number k counts only the non-constant decisions (see decisions_in)
    std::map<const exprt *, std::size_t> index;
    std::vector<std::optional<mcdc_evaluationt>> step_evaluations;
    std::size_t k = 0;
    for(std::size_t d = 0; d < found.size(); ++d)
    {
      step_evaluations.emplace_back();
      if(constant_decision(*found[d].root, ns))
        continue;
      step_evaluations.back() = mcdc_evaluationt{
        key(pc, *line, k++),
        from_expr(ns, function, *found[d].root),
        and_exprt(step.guard, ssa_found[d].context),
        nil_exprt(),
        {}};
      index[ssa_found[d].root] = d;
    }

    // Replace the leaves of each decision by condition symbols, inside out:
    // a decision nested in a leaf (?: in &&) is replaced in the leaf first.
    std::function<exprt(const exprt &)> rewrite;
    const auto rewrite_operands = [&](const exprt &e) {
      exprt result = e;
      for(std::size_t i = 0; i < e.operands().size(); ++i)
        result.operands()[i] = rewrite(e.operands()[i]);
      return result;
    };
    rewrite = [&](const exprt &e) {
      const auto it = index.find(&e);
      if(it == index.end())
        return rewrite_operands(e);
      auto &evaluation = *step_evaluations[it->second];
      std::vector<const exprt *> ssa_leaves, leaves;
      collect_leaves(e, ssa_leaves);
      collect_leaves(*found[it->second].root, leaves);
      std::vector<exprt> replacements;
      for(std::size_t i = 0; i < ssa_leaves.size(); ++i)
      {
        // a decision with a single condition is its own leaf (c ? x : y)
        exprt leaf = ssa_leaves[i] == &e ? rewrite_operands(e)
                                         : rewrite(*ssa_leaves[i]);
        if(simplify_expr(*leaves[i], ns).is_constant())
        {
          replacements.push_back(leaf);
          continue;
        }
        evaluation.conditions.push_back(
          new_condition(leaf, from_expr(ns, function, *leaves[i])));
        replacements.push_back(evaluation.conditions.back().symbol);
      }
      std::size_t next = 0;
      evaluation.decision = substitute_leaves(e, replacements, next);
      return evaluation.decision;
    };
    step.ssa_rhs = rewrite(step.ssa_rhs);
    step.cond_expr = equal_exprt(step.ssa_lhs, step.ssa_rhs);

    for(auto &evaluation : step_evaluations)
    {
      if(evaluation.has_value() && !evaluation->conditions.empty())
        evaluations.push_back(std::move(*evaluation));
    }
  }

  return evaluations;
}

void proof_coverage_checkert::report_mcdc(
  const std::string &mode,
  const symex_target_equationt::SSA_stepst &steps,
  const std::vector<mcdc_evaluationt> &evaluations,
  solver_factoryt &solver_factory)
{
  if(evaluations.empty())
  {
    log.result() << "MC/DC (" << mode << "): no decisions with conditions"
                 << messaget::eom;
    return;
  }

  // The definitions of the conditions are switched by activation literals,
  // in the run (program without assertions) and, in result mode, in the
  // program with the negated assertion.
  struct switched_solvert
  {
    std::unique_ptr<solver_factoryt::solvert> solver;
    std::vector<std::vector<exprt>> activations;
    std::vector<exprt> all;
  };
  std::size_t activation_counter = 0;
  const auto make_solver = [&](bool with_assertions) {
    switched_solvert s;
    s.solver = solver_factory.get_solver();
    auto &decision_procedure = s.solver->decision_procedure();
    convert_program(steps, with_assertions, decision_procedure, false);
    for(const auto &evaluation : evaluations)
    {
      s.activations.emplace_back();
      for(const auto &condition : evaluation.conditions)
      {
        const symbol_exprt activation(
          "proofcov::condition_act#" + std::to_string(activation_counter++),
          bool_typet());
        decision_procedure.set_to_true(
          implies_exprt(activation, condition.definition));
        s.activations.back().push_back(decision_procedure.handle(activation));
        s.all.push_back(s.activations.back().back());
      }
    }
    return s;
  };
  // all activations except the one of condition \p k of evaluation \p e
  const auto all_but =
    [](const switched_solvert &s, std::size_t e, std::size_t k) {
      std::vector<exprt> assumptions;
      for(std::size_t i = 0; i < s.activations.size(); ++i)
      {
        for(std::size_t j = 0; j < s.activations[i].size(); ++j)
        {
          if(i != e || j != k)
            assumptions.push_back(s.activations[i][j]);
        }
      }
      return assumptions;
    };
  const auto check = [](
                       stack_decision_proceduret &decision_procedure,
                       const std::vector<exprt> &assumptions) {
    decision_procedure.push(assumptions);
    const auto result = decision_procedure();
    decision_procedure.pop();
    return result;
  };

  // the run: the values of the guards, decisions and conditions
  auto run = make_solver(false);
  auto &run_dp = run.solver->decision_procedure();
  struct handlest
  {
    exprt reached, decision, not_decision;
    std::vector<exprt> conditions;
  };
  std::vector<handlest> handles;
  for(const auto &evaluation : evaluations)
  {
    handlest h{
      run_dp.handle(evaluation.reached),
      run_dp.handle(evaluation.decision),
      run_dp.handle(not_exprt(evaluation.decision)),
      {}};
    for(const auto &condition : evaluation.conditions)
      h.conditions.push_back(run_dp.handle(condition.symbol));
    handles.push_back(std::move(h));
  }

  run_dp.push(run.all);
  if(run_dp() != decision_proceduret::resultt::D_SATISFIABLE)
  {
    run_dp.pop();
    log.warning() << "MC/DC: the program has no run within the bounds"
                  << messaget::eom;
    return;
  }
  struct valuest
  {
    bool reached, decision;
    std::vector<bool> conditions;
    /// not short-circuited
    std::vector<bool> evaluated;
  };
  std::vector<valuest> values;
  exprt::operandst differs;
  for(const auto &h : handles)
  {
    valuest v{
      run_dp.get(h.reached).is_true(),
      run_dp.get(h.decision).is_true(),
      {}};
    differs.push_back(v.reached ? not_exprt(h.reached) : h.reached);
    if(v.reached)
    {
      differs.push_back(v.decision ? h.not_decision : h.decision);
      for(const auto &c : h.conditions)
        v.conditions.push_back(run_dp.get(c).is_true());

      // A condition that C does not evaluate (short circuit) may have no
      // unique value (e.g. b / a with a == 0); only the others must.
      const auto &ev = evaluations[values.size()];
      std::map<irep_idt, bool> by_symbol;
      for(std::size_t k = 0; k < ev.conditions.size(); ++k)
        by_symbol[ev.conditions[k].symbol.get_identifier()] = v.conditions[k];
      std::set<irep_idt> evaluated;
      evaluate_short_circuit(ev.decision, by_symbol, evaluated, ns);
      for(std::size_t k = 0; k < ev.conditions.size(); ++k)
      {
        v.evaluated.push_back(
          evaluated.count(ev.conditions[k].symbol.get_identifier()) != 0);
        if(v.evaluated.back())
        {
          const auto &c = h.conditions[k];
          differs.push_back(v.conditions[k] ? not_exprt(c) : c);
        }
      }
    }
    values.push_back(std::move(v));
  }
  run_dp.pop();

  // the run must be the only one (concrete inputs)
  const exprt other_run = run_dp.handle(disjunction(differs));
  std::vector<exprt> assumptions = run.all;
  assumptions.push_back(other_run);
  if(check(run_dp, assumptions) != decision_proceduret::resultt::D_UNSATISFIABLE)
  {
    log.warning() << "MC/DC: the test has more than one run (nondeterministic "
                  << "inputs?); MC/DC needs concrete inputs" << messaget::eom;
    return;
  }

  // independence of every condition of every reached evaluation
  std::vector<std::vector<bool>> independent(evaluations.size());
  if(mode == "decision")
  {
    for(std::size_t e = 0; e < evaluations.size(); ++e)
    {
      if(!values[e].reached)
        continue;
      for(std::size_t k = 0; k < evaluations[e].conditions.size(); ++k)
      {
        if(!values[e].evaluated[k])
        {
          independent[e].push_back(false);
          continue;
        }
        auto a = all_but(run, e, k);
        a.push_back(handles[e].reached);
        a.push_back(
          values[e].decision ? handles[e].not_decision : handles[e].decision);
        independent[e].push_back(
          check(run_dp, a) == decision_proceduret::resultt::D_SATISFIABLE);
      }
    }
  }
  else
  {
    auto result = make_solver(true);
    auto &result_dp = result.solver->decision_procedure();
    if(
      check(result_dp, result.all) !=
      decision_proceduret::resultt::D_UNSATISFIABLE)
    {
      log.warning() << "MC/DC: the assertion does not hold" << messaget::eom;
      return;
    }
    for(std::size_t e = 0; e < evaluations.size(); ++e)
    {
      if(!values[e].reached)
        continue;
      for(std::size_t k = 0; k < evaluations[e].conditions.size(); ++k)
      {
        if(!values[e].evaluated[k])
        {
          independent[e].push_back(false);
          continue;
        }
        independent[e].push_back(
          check(result_dp, all_but(result, e, k)) ==
          decision_proceduret::resultt::D_SATISFIABLE);
      }
    }
  }

  // Report per decision, in the order of first evaluation. A condition
  // outcome (true/false) is covered when the condition was independent
  // with that value in some evaluation.
  std::vector<std::string> order;
  std::map<std::string, std::vector<std::size_t>> by_decision;
  for(std::size_t e = 0; e < evaluations.size(); ++e)
  {
    if(!values[e].reached)
      continue;
    const auto &ev = evaluations[e];
    const std::string &key = ev.key;
    if(by_decision.count(key) == 0)
      order.push_back(key);
    by_decision[key].push_back(e);
  }

  log.result() << "MC/DC (" << mode << " mode): "
               << (mode == "decision"
                     ? "conditions that independently decide their decision"
                     : "conditions that independently decide the assertion")
               << messaget::eom;
  std::size_t covered_total = 0, outcomes_total = 0;
  std::vector<std::string> machine;
  for(const auto &key : order)
  {
    const auto &evs = by_decision[key];
    const auto &first = evaluations[evs.front()];
    log.result() << key << "  " << first.text << messaget::eom;
    std::vector<std::set<bool>> covered(first.conditions.size());
    std::size_t number = 0;
    for(const auto e : evs)
    {
      const auto &ev = evaluations[e];
      std::ostringstream line;
      line << "  evaluation " << ++number << ": decision "
           << (values[e].decision ? "true" : "false");
      for(std::size_t k = 0; k < ev.conditions.size(); ++k)
      {
        const bool value = values[e].conditions[k];
        line << "; " << ev.conditions[k].text;
        if(!values[e].evaluated[k])
          line << " not evaluated";
        else
          line << " = " << (value ? "true" : "false") << " "
               << (independent[e][k] ? "independent" : "masked");
        if(independent[e][k] && k < covered.size())
          covered[k].insert(value);
      }
      log.result() << line.str() << messaget::eom;
      for(std::size_t k = 0; k < ev.conditions.size(); ++k)
      {
        machine.push_back(
          "MCDC: " + mode + " " + key + " c" + std::to_string(k) +
          " value=" +
          (values[e].evaluated[k] ? std::to_string(values[e].conditions[k])
                                  : std::string("-")) +
          " outcome=" + std::to_string(values[e].decision) + " " +
          (independent[e][k] ? "independent" : "masked") + " " +
          ev.conditions[k].text);
      }
    }
    std::size_t covered_here = 0;
    std::ostringstream which;
    for(std::size_t k = 0; k < covered.size(); ++k)
    {
      covered_here += covered[k].size();
      which << (k == 0 ? "" : ", ") << first.conditions[k].text << ": "
            << (covered[k].empty()          ? "none"
                : covered[k].size() == 2    ? "true false"
                : covered[k].count(true)    ? "true"
                                            : "false");
    }
    log.result() << "  condition outcomes covered: " << covered_here << "/"
                 << 2 * covered.size() << " (" << which.str() << ")"
                 << messaget::eom;
    covered_total += covered_here;
    outcomes_total += 2 * covered.size();
  }
  log.result() << "MC/DC condition outcomes covered: " << covered_total << "/"
               << outcomes_total << messaget::eom;

  // one line per condition and evaluation, for combining tests
  for(const auto &m : machine)
    log.status() << m << messaget::eom;
}
