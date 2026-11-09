/*******************************************************************\

Module: Proof Coverage

Author: proofcov

\*******************************************************************/

/// \file
/// Proof-based coverage

#include "proof_coverage_checker.h"

#include <util/cmdline.h>
#include <util/exception_utils.h>
#include <util/exit_codes.h>
#include <util/find_symbols.h>
#include <util/options.h>
#include <util/std_expr.h>
#include <util/ui_message.h>

#include <goto-symex/build_goto_trace.h>
#include <solvers/conflict_provider.h>

#include "bmc_util.h"
#include "proof_coverage_mus.h"
#include "solver_factory.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <limits>
#include <numeric>
#include <set>

void parse_proof_coverage_options(const cmdlinet &cmdline, optionst &options)
{
  if(!cmdline.isset("proof-coverage"))
    return;

  options.set_option("proof-coverage", true);
  for(const char *flag :
      {"proof-coverage-cores",
       "proof-coverage-trace",
       "proof-coverage-branch",
       "proof-coverage-track-undef",
       "proof-coverage-experiment",
       "proof-coverage-annotate",
       "proof-coverage-verify"})
  {
    options.set_option(flag, cmdline.isset(flag));
  }

  if(cmdline.isset("proof-coverage-mcdc"))
  {
    const std::string mode = cmdline.get_value("proof-coverage-mcdc");
    if(mode != "decision" && mode != "result")
    {
      throw invalid_command_line_argument_exceptiont(
        "must be decision or result", "--proof-coverage-mcdc");
    }
    options.set_option("proof-coverage-mcdc", mode);
  }

  if(cmdline.isset("proof-coverage-max-cores"))
  {
    options.set_option("proof-coverage-cores", true);
    options.set_option(
      "proof-coverage-max-cores",
      cmdline.get_value("proof-coverage-max-cores"));
  }

  // Constant propagation and simplification in symex would discharge
  // statements before the solver sees them, so they could not be covered.
  options.set_option("propagation", false);
  options.set_option("simplify", false);

  // The SAT preprocessor may eliminate the activation literals.
  options.set_option("sat-preprocessor", false);
}

proof_coverage_checkert::proof_coverage_checkert(
  const optionst &options,
  ui_message_handlert &ui_message_handler,
  abstract_goto_modelt &goto_model)
  : multi_path_symex_only_checkert(options, ui_message_handler, goto_model)
{
}

int proof_coverage_checkert::run()
{
  generate_equation();

  if(select_assertions() == 0)
  {
    log.error() << "proof coverage requires at least one assertion in user "
                << "code" << messaget::eom;
    return CPROVER_EXIT_INCORRECT_TASK;
  }

  collect_soft_constraints();
  collect_coverable_lines();

  log.status() << "Proof coverage: " << soft_constraints.size()
               << " soft constraints" << messaget::eom;

  // The steps before conversion, for the values of the run (see to_core).
  // Only needed when a not-taken phi side mentions several branch
  // conditions, or for --proof-coverage-trace.
  const bool tag_trace = options.get_bool_option("proof-coverage-trace");

  // Whether a phi side may be credited to fewer lines than its condition
  // mentions: when the condition joins several branches, or contains a
  // Boolean variable that stands for a branch (e.g. the temporary of a
  // condition with a function call), which has no line of its own here.
  const auto refinable = [this](const soft_constraintt &c) {
    if(c.merge_condition.is_nil())
      return false;
    if(c.lines.size() > 1)
      return true;
    for(const auto &symbol : find_symbols(c.merge_condition))
    {
      if(guards.count(symbol.get_identifier()) == 0)
        return true;
    }
    return false;
  };
  std::optional<symex_target_equationt::SSA_stepst> unconverted_steps;
  if(
    tag_trace || std::any_of(
                   soft_constraints.begin(),
                   soft_constraints.end(),
                   refinable))
  {
    unconverted_steps = equation.SSA_steps;
  }

  // The steps before conversion, with the conditions of the decisions as
  // symbols of their own, for --proof-coverage-mcdc
  const std::string mcdc_mode = options.get_option("proof-coverage-mcdc");
  symex_target_equationt::SSA_stepst mcdc_steps;
  std::vector<mcdc_evaluationt> mcdc_evaluations;
  if(!mcdc_mode.empty())
  {
    mcdc_steps = equation.SSA_steps;
    mcdc_evaluations = collect_mcdc(mcdc_steps);
  }

  solver_factoryt solver_factory(options, ns, ui_message_handler, false);
  const auto solver = solver_factory.get_solver();
  auto &decision_procedure = solver->decision_procedure();

  const auto conflict_provider =
    dynamic_cast<const conflict_providert *>(&decision_procedure);
  if(conflict_provider == nullptr)
  {
    log.error() << "proof coverage requires a SAT-based decision procedure"
                << messaget::eom;
    return CPROVER_EXIT_USAGE_ERROR;
  }

  // hard constraints: everything that has not been made soft
  equation.convert(decision_procedure);

  std::vector<exprt> activation_handles;
  activation_handles.reserve(soft_constraints.size());
  for(std::size_t i = 0; i < soft_constraints.size(); ++i)
  {
    const symbol_exprt activation(
      "proofcov::act#" + std::to_string(i), bool_typet());
    decision_procedure.set_to_true(
      implies_exprt(activation, soft_constraints[i].constraint));
    activation_handles.push_back(decision_procedure.handle(activation));
  }

  proof_coverage_subset_solvert subset_solver(
    decision_procedure, *conflict_provider, std::move(activation_handles));

  proof_coverage_seedt all(soft_constraints.size());
  std::iota(all.begin(), all.end(), 0);

  if(subset_solver.check_subset(all))
    return report_failure(decision_procedure);

  log.result() << "Test pass: the assertions hold" << messaget::eom;

  // computed on first use
  std::optional<std::optional<guard_valuest>> run;
  std::optional<std::optional<std::vector<std::optional<bool>>>> executed;

  const auto to_core = [&](const proof_coverage_seedt &seed) {
    proof_coverage_coret core;
    for(const auto i : seed)
    {
      const auto &soft = soft_constraints[i];
      core.constraint_names.push_back(soft.name);

      // A phi side the run took is credited to the branch conditions that
      // decide its condition in the run, not to every condition the phi
      // condition mentions: for a not-taken branch the condition at which
      // the run left the path (the later ones were never evaluated), for
      // merged paths the path the run took. A side the run did not take
      // (non-trace) keeps all its lines.
      if(refinable(soft) && unconverted_steps.has_value())
      {
        if(!run.has_value())
          run = run_guard_values(*unconverted_steps, solver_factory);
        proof_coverage_linest lines;
        if(
          run->has_value() &&
          evaluate(soft.merge_condition, **run) ==
            std::optional<bool>{soft.merge_value} &&
          deciding_lines(
            soft.merge_condition, soft.merge_value, **run, lines) &&
          !lines.empty())
        {
          core.lines.insert(lines.begin(), lines.end());
          continue;
        }
      }

      core.lines.insert(soft.lines.begin(), soft.lines.end());
    }

    // A core is trace when everything it uses was executed by the run:
    // statements on the run's path and phi sides the run took.
    if(tag_trace)
    {
      if(!executed.has_value())
        executed = run_executed(*unconverted_steps, solver_factory);
      if(!executed->has_value())
      {
        core.trace = "unknown (the test has more than one run)";
        return core;
      }
      std::set<std::pair<std::size_t, std::string>> missing;
      for(const auto i : seed)
      {
        const auto &soft = soft_constraints[i];
        if((**executed)[i] != std::optional<bool>{false} || soft.lines.empty())
          continue;
        const bool phi = soft.name.rfind("phi.", 0) == 0;
        std::size_t line = soft.lines.begin()->line;
        if(phi)
        {
          // A phi side is named after its innermost branch, the last guard
          // on its path. Which C branch (then/else) a phi side merges is
          // not recorded, only that the run did not take it.
          std::size_t latest = 0;
          for(const auto &symbol : find_symbols(soft.executed))
          {
            const auto guard = guards.find(symbol.get_identifier());
            if(guard != guards.end() && guard->second.order >= latest)
            {
              latest = guard->second.order;
              line = guard->second.line.line;
            }
          }
        }
        missing.emplace(
          line, phi ? "untaken side of the branch at line" : "line");
      }
      core.trace = missing.empty() ? "trace" : "non-trace";
      for(const auto &m : missing)
      {
        core.not_executed += (core.not_executed.empty() ? "" : ", ") +
                             m.second + " " + std::to_string(m.first);
      }
    }
    return core;
  };

  proof_coverage_reportt report(options, ui_message_handler, program);

  // Independent check of a reported MUS: unsatisfiable, and satisfiable
  // without any one of its soft constraints.
  const auto verify = [&](const proof_coverage_seedt &mus, std::size_t number) {
    if(!options.get_bool_option("proof-coverage-verify"))
      return;
    if(subset_solver.check_subset(mus))
    {
      log.error() << "proof " << number << " is satisfiable" << messaget::eom;
      return;
    }
    bool minimal = true;
    for(std::size_t i = 0; i < mus.size(); ++i)
    {
      proof_coverage_seedt smaller = mus;
      smaller.erase(smaller.begin() + i);
      if(!subset_solver.check_subset(smaller))
      {
        minimal = false;
        log.error() << "proof " << number << " is not minimal: "
                    << soft_constraints[mus[i]].name << " is not needed"
                    << messaget::eom;
      }
    }
    if(minimal)
      log.status() << "proof " << number << " verified" << messaget::eom;
  };

  if(!options.get_bool_option("proof-coverage-cores"))
  {
    const auto mus = subset_solver.shrink(subset_solver.last_core());
    verify(mus, 1);
    report(1, to_core(mus));
    if(!mcdc_mode.empty())
      report_mcdc(mcdc_mode, mcdc_steps, mcdc_evaluations, solver_factory);
    return CPROVER_EXIT_VERIFICATION_SAFE;
  }

  const std::size_t max_cores =
    options.get_unsigned_int_option("proof-coverage-max-cores");
  proof_coverage_marcot marco(subset_solver, ui_message_handler);
  std::size_t number_of_cores = 0;
  while(const auto mus = marco.next_mus())
  {
    verify(*mus, number_of_cores + 1);
    report(++number_of_cores, to_core(*mus));
    if(max_cores != 0 && number_of_cores >= max_cores)
      break;
  }

  log.result() << "Proof cores found: " << number_of_cores << messaget::eom;
  if(!mcdc_mode.empty())
    report_mcdc(mcdc_mode, mcdc_steps, mcdc_evaluations, solver_factory);
  return CPROVER_EXIT_VERIFICATION_SAFE;
}

int proof_coverage_checkert::report_failure(
  stack_decision_proceduret &decision_procedure)
{
  log.result() << "Test fail: an assertion can be violated" << messaget::eom;

  if(options.get_bool_option("trace"))
  {
    goto_tracet goto_trace;
    // stop at the first violated user assertion (the others are ignored)
    const auto is_violated_assertion =
      [](
        symex_target_equationt::SSA_stepst::const_iterator step,
        const decision_proceduret &decision_procedure) {
        return step->is_assert() && !step->ignore &&
               decision_procedure.get(step->cond_handle).is_false();
      };
    build_goto_trace(
      equation, is_violated_assertion, decision_procedure, ns, goto_trace);
    output_error_trace(
      goto_trace, ns, trace_optionst(options), ui_message_handler);
  }

  return CPROVER_EXIT_VERIFICATION_UNSAFE;
}

std::size_t proof_coverage_checkert::select_assertions()
{
  std::size_t count = 0;
  for(auto &step : equation.SSA_steps)
  {
    if(!step.is_assert() || step.ignore)
      continue;

    // Only user assertions are proven; generated checks (bounds,
    // overflow, unwinding, ...) are not part of the proof obligation.
    const auto &source_location = step.source.pc->source_location();
    const auto line = proof_coverage_user_line(source_location);
    if(line.has_value() && source_location.get_property_class() == "assertion")
    {
      program.assertion_lines.insert(*line);
      ++count;
    }
    else
      step.ignore = true;
  }
  return count;
}

void proof_coverage_checkert::collect_soft_constraints()
{
  const bool track_undef =
    options.get_bool_option("proof-coverage-track-undef");

  // to name constraints lineN.0, lineN.1, ... as the Python prototype
  std::map<proof_coverage_linet, std::size_t> constraints_per_line;

  // phi sides whose executed condition still needs the path condition of
  // the merge, which the next step that is not a phi or guard carries
  std::size_t pending_phis = soft_constraints.size();

  for(auto &step : equation.SSA_steps)
  {
    const bool merge_step =
      step.is_assignment() &&
      (step.assignment_type == symex_targett::assignment_typet::PHI ||
       step.assignment_type == symex_targett::assignment_typet::GUARD);
    if(!merge_step && !step.ignore)
    {
      for(; pending_phis < soft_constraints.size(); ++pending_phis)
      {
        auto &executed = soft_constraints[pending_phis].executed;
        if(
          soft_constraints[pending_phis].merge_condition.is_not_nil() &&
          executed.is_not_nil())
        {
          executed = and_exprt(step.guard, executed);
        }
      }
    }

    if(!step.is_assignment() || step.ignore)
      continue;

    const auto line =
      proof_coverage_user_line(step.source.pc->source_location());
    if(!line.has_value())
      continue;

    switch(step.assignment_type)
    {
    case symex_targett::assignment_typet::GUARD:
      // The guard definition stays hard: the guard symbol merely names the
      // branch condition, which the phi functions of the Python prototype
      // use directly. The condition is covered through those phis.
      guards[step.ssa_lhs.get_identifier()] =
        guardt{*line, step.ssa_rhs, guards.size()};
      continue;

    case symex_targett::assignment_typet::STATE:
    {
      exprt constraint = step.cond_expr;
      if(track_undef)
        constraint = and_exprt(constraint, not_exprt(undef(step.ssa_lhs)));
      record_branch_membership(step.guard, *line);
      add_soft_constraint(
        std::move(constraint),
        {*line},
        "line" + std::to_string(line->line) + "." +
          std::to_string(constraints_per_line[*line]++),
        nil_exprt(),
        false,
        step.guard);
      break;
    }

    case symex_targett::assignment_typet::PHI:
      add_phi(step, *line);
      break;

    case symex_targett::assignment_typet::HIDDEN:
    case symex_targett::assignment_typet::VISIBLE_ACTUAL_PARAMETER:
    case symex_targett::assignment_typet::HIDDEN_ACTUAL_PARAMETER:
      // parameter passing and internal bookkeeping stay hard
      continue;
    }

    // Encoded by us rather than by the equation. Not `ignore`d, as that
    // would drop the step's guard and hide it from error traces.
    step.converted = true;
  }
}

void proof_coverage_checkert::add_soft_constraint(
  exprt constraint,
  proof_coverage_linest lines,
  std::string name,
  exprt merge_condition,
  bool merge_value,
  exprt executed)
{
  soft_constraints.push_back(soft_constraintt{
    std::move(constraint),
    std::move(lines),
    std::move(name),
    std::move(merge_condition),
    merge_value,
    std::move(executed)});
}

std::optional<proof_coverage_checkert::guard_valuest>
proof_coverage_checkert::run_guard_values(
  symex_target_equationt::SSA_stepst steps,
  solver_factoryt &solver_factory)
{
  // the guards (and other Boolean variables) whose values are needed
  std::set<irep_idt> needed;
  for(const auto &soft : soft_constraints)
  {
    if(soft.merge_condition.is_nil())
      continue;
    for(const auto &symbol : find_symbols(soft.merge_condition))
    {
      if(guards.count(symbol.get_identifier()) != 0)
        needed.insert(symbol.get_identifier());
      else if(symbol.type().id() == ID_bool)
      {
        needed.insert(symbol.get_identifier());
        condition_symbol_lines.emplace(symbol.get_identifier(), proof_coverage_linet{});
      }
    }
  }
  // the line and position of the assignment of each such variable
  std::size_t guards_before = 0;
  for(const auto &step : steps)
  {
    if(!step.is_assignment())
      continue;
    const irep_idt &id = step.ssa_lhs.get_identifier();
    if(guards.count(id) != 0)
    {
      ++guards_before;
      continue;
    }
    const auto entry = condition_symbol_lines.find(id);
    const auto line =
      proof_coverage_user_line(step.source.pc->source_location());
    if(entry != condition_symbol_lines.end() && line.has_value())
    {
      entry->second = *line;
      condition_symbol_order[id] = 2 * guards_before;
    }
  }

  // the program without the assertions, in a solver of its own
  const auto solver = solver_factory.get_solver();
  auto &decision_procedure = solver->decision_procedure();
  convert_program(std::move(steps), false, decision_procedure);

  if(decision_procedure() != decision_proceduret::resultt::D_SATISFIABLE)
    return {};

  guard_valuest run;
  exprt::operandst differs;
  for(const auto &id : needed)
  {
    const symbol_exprt guard(id, bool_typet());
    const exprt value = decision_procedure.get(guard);
    if(!value.is_constant())
      return {};
    run[id] = value.is_true();
    differs.push_back(value.is_true() ? not_exprt(guard) : exprt(guard));
  }

  // the values must be the only ones possible
  decision_procedure.set_to_true(disjunction(differs));
  if(decision_procedure() != decision_proceduret::resultt::D_UNSATISFIABLE)
    return {};

  return run;
}

void proof_coverage_checkert::convert_program(
  symex_target_equationt::SSA_stepst steps,
  bool with_assertions,
  decision_proceduret &decision_procedure,
  bool add_soft_constraints)
{
  std::swap(equation.SSA_steps, steps);
  if(!with_assertions)
  {
    for(auto &step : equation.SSA_steps)
    {
      if(step.is_assert())
        step.ignore = true;
    }
  }
  equation.convert(decision_procedure);
  std::swap(equation.SSA_steps, steps);

  if(add_soft_constraints)
  {
    for(const auto &soft : soft_constraints)
      decision_procedure.set_to_true(soft.constraint);
  }
}

std::optional<std::vector<std::optional<bool>>>
proof_coverage_checkert::run_executed(
  symex_target_equationt::SSA_stepst steps,
  solver_factoryt &solver_factory)
{
  // the run: the program without the assertions, in a solver of its own
  const auto solver = solver_factory.get_solver();
  auto &decision_procedure = solver->decision_procedure();
  convert_program(std::move(steps), false, decision_procedure);

  std::vector<exprt> handles;
  for(const auto &soft : soft_constraints)
  {
    handles.push_back(
      soft.executed.is_nil() ? nil_exprt()
                             : decision_procedure.handle(soft.executed));
  }
  if(decision_procedure() != decision_proceduret::resultt::D_SATISFIABLE)
    return {};

  std::vector<std::optional<bool>> result;
  exprt::operandst differs;
  for(const auto &h : handles)
  {
    if(h.is_nil())
    {
      result.emplace_back();
      continue;
    }
    const exprt value = decision_procedure.get(h);
    if(!value.is_constant())
      return {};
    result.emplace_back(value.is_true());
    differs.push_back(value.is_true() ? not_exprt(h) : h);
  }

  // the values must be the only ones possible (concrete inputs)
  decision_procedure.set_to_true(disjunction(differs));
  if(decision_procedure() != decision_proceduret::resultt::D_UNSATISFIABLE)
    return {};
  return result;
}

std::optional<bool> proof_coverage_checkert::evaluate(
  const exprt &expr,
  const guard_valuest &run) const
{
  if(expr.is_true())
    return true;
  if(expr.is_false())
    return false;
  if(expr.id() == ID_symbol)
  {
    const auto value = run.find(to_symbol_expr(expr).get_identifier());
    if(value == run.end())
      return {};
    return value->second;
  }
  if(expr.id() == ID_not)
  {
    const auto op = evaluate(to_not_expr(expr).op(), run);
    if(!op.has_value())
      return {};
    return !*op;
  }
  if(expr.id() == ID_and || expr.id() == ID_or)
  {
    // and: false if any operand is false; or: true if any is true
    const bool absorbing = expr.id() == ID_or;
    bool known = true;
    for(const auto &op : expr.operands())
    {
      const auto value = evaluate(op, run);
      if(!value.has_value())
        known = false;
      else if(*value == absorbing)
        return absorbing;
    }
    if(!known)
      return {};
    return !absorbing;
  }
  return {};
}

std::size_t proof_coverage_checkert::first_guard(const exprt &expr) const
{
  std::size_t first = std::numeric_limits<std::size_t>::max();
  for(const auto &symbol : find_symbols(expr))
  {
    const auto guard = guards.find(symbol.get_identifier());
    if(guard != guards.end())
      first = std::min(first, 2 * guard->second.order + 1);
    const auto variable =
      condition_symbol_order.find(symbol.get_identifier());
    if(variable != condition_symbol_order.end())
      first = std::min(first, variable->second);
  }
  return first;
}

bool proof_coverage_checkert::deciding_lines(
  const exprt &expr,
  bool value,
  const guard_valuest &run,
  proof_coverage_linest &lines) const
{
  if(expr.is_true() || expr.is_false())
    return true;

  if(expr.id() == ID_symbol)
  {
    const irep_idt &id = to_symbol_expr(expr).get_identifier();
    const auto guard = guards.find(id);
    if(guard != guards.end())
    {
      lines.insert(guard->second.line);
      return true;
    }
    // e.g. the temporary holding a condition with a function call
    const auto variable = condition_symbol_lines.find(id);
    if(variable == condition_symbol_lines.end() || variable->second.line == 0)
      return false;
    lines.insert(variable->second);
    return true;
  }

  if(expr.id() == ID_not)
    return deciding_lines(to_not_expr(expr).op(), !value, run, lines);

  if(expr.id() != ID_and && expr.id() != ID_or)
    return false;

  // A false conjunction (true disjunction) is decided by its first false
  // (true) operand in path order; a true conjunction (false disjunction)
  // needs all of its operands.
  const bool decided_by_one = value == (expr.id() == ID_or);
  if(!decided_by_one)
  {
    for(const auto &op : expr.operands())
    {
      if(!deciding_lines(op, value, run, lines))
        return false;
    }
    return true;
  }

  const exprt *decider = nullptr;
  for(const auto &op : expr.operands())
  {
    if(
      evaluate(op, run) == std::optional<bool>{value} &&
      (decider == nullptr || first_guard(op) < first_guard(*decider)))
    {
      decider = &op;
    }
  }
  return decider != nullptr && deciding_lines(*decider, value, run, lines);
}

void proof_coverage_checkert::add_phi(
  const SSA_stept &step,
  const proof_coverage_linet &line)
{
  const exprt &lhs = step.ssa_full_lhs;
  const std::string variable = id2string(step.ssa_lhs.get_identifier());

  if(step.ssa_rhs.id() != ID_if)
  {
    // a merge where only one side defines the variable
    exprt constraint = step.cond_expr;
    if(options.get_bool_option("proof-coverage-track-undef"))
      constraint = and_exprt(constraint, not_exprt(undef(step.ssa_lhs)));
    add_soft_constraint(
      std::move(constraint),
      {line},
      "phi." + std::to_string(line.line) + "." + variable);
    return;
  }

  const if_exprt &phi = to_if_expr(step.ssa_rhs);

  // the condition tells how the merged branches are nested
  record_branch_membership(phi.cond(), {});

  // The phi belongs to the branching statement(s) it merges, not to the
  // join point where symex places it.
  proof_coverage_linest lines;
  for(const auto &symbol : find_symbols(phi.cond()))
  {
    const auto guard = guards.find(symbol.get_identifier());
    if(guard != guards.end())
      lines.insert(guard->second.line);
  }
  if(lines.empty())
    lines.insert(line);

  const std::string suffix =
    std::to_string(lines.begin()->line) + "." + variable;

  // A phi side is executed when the run reaches the merge (e.g. the end of
  // an inlined call that is really made) through that side.
  // (the path condition is added in collect_soft_constraints: the guard of
  // a phi step is true, the next step has the merged path condition)
  const exprt taken_if = phi.cond();
  const exprt taken_else = not_exprt(phi.cond());

  if(!options.get_bool_option("proof-coverage-track-undef"))
  {
    add_soft_constraint(
      implies_exprt(phi.cond(), equal_exprt(lhs, phi.true_case())),
      lines,
      "phi.if." + suffix,
      phi.cond(),
      true,
      taken_if);
    add_soft_constraint(
      implies_exprt(not_exprt(phi.cond()), equal_exprt(lhs, phi.false_case())),
      lines,
      "phi.else." + suffix,
      phi.cond(),
      false,
      taken_else);
    return;
  }

  // Each side of the phi gets a selector: either the side is enforced
  // (selector true), or the phi is bypassed because the condition or the
  // incoming value is undefined.
  const exprt condition_undef = any_undef(phi.cond());
  const exprt lhs_defined = not_exprt(undef(step.ssa_lhs));

  const symbol_exprt select_if(
    "proofcov::select#" + std::to_string(selector_counter++), bool_typet());
  add_soft_constraint(
    or_exprt(select_if, condition_undef, undef(phi.true_case())),
    lines,
    "phi.if." + suffix + ".cond",
    phi.cond(),
    true,
    taken_if);
  add_soft_constraint(
    or_exprt(
      not_exprt(phi.cond()),
      and_exprt(equal_exprt(lhs, phi.true_case()), lhs_defined),
      not_exprt(select_if)),
    lines,
    "phi.if." + suffix,
    phi.cond(),
    true,
    taken_if);

  const symbol_exprt select_else(
    "proofcov::select#" + std::to_string(selector_counter++), bool_typet());
  add_soft_constraint(
    or_exprt(select_else, condition_undef, undef(phi.false_case())),
    lines,
    "phi.else." + suffix + ".cond",
    phi.cond(),
    false,
    taken_else);
  add_soft_constraint(
    or_exprt(
      phi.cond(),
      and_exprt(equal_exprt(lhs, phi.false_case()), lhs_defined),
      not_exprt(select_else)),
    lines,
    "phi.else." + suffix,
    phi.cond(),
    false,
    taken_else);
}

/// Collects the conjuncts of a (possibly nested) conjunction
static void collect_conjuncts(const exprt &expr, exprt::operandst &dest)
{
  if(expr.id() == ID_and)
  {
    for(const auto &op : expr.operands())
      collect_conjuncts(op, dest);
  }
  else
    dest.push_back(expr);
}

void proof_coverage_checkert::record_branch_membership(
  const exprt &path_condition,
  const std::optional<proof_coverage_linet> &line)
{
  if(path_condition.is_nil())
    return;

  exprt::operandst conjuncts;
  collect_conjuncts(path_condition, conjuncts);

  // the branch conditions on the path, with the outcome taken
  std::vector<std::pair<const guardt *, bool>> path;
  for(const auto &conjunct : conjuncts)
  {
    bool polarity = true;
    const exprt *guard_expr = &conjunct;
    if(conjunct.id() == ID_not)
    {
      polarity = false;
      guard_expr = &to_not_expr(conjunct).op();
    }

    if(guard_expr->id() != ID_symbol)
      continue;

    const auto guard =
      guards.find(to_symbol_expr(*guard_expr).get_identifier());
    if(guard != guards.end())
      path.emplace_back(&guard->second, polarity);
  }

  for(const auto &outer : path)
  {
    proof_coverage_linest lines;
    if(line.has_value())
      lines.insert(*line);

    // a condition that is decided later on the same path is a statement
    // inside this branch
    for(const auto &inner : path)
    {
      if(inner.first->order > outer.first->order)
        lines.insert(inner.first->line);
    }

    // only branches that contain statements are counted
    if(!lines.empty())
      branch_lines(outer.first->line, outer.second)
        .insert(lines.begin(), lines.end());
  }
}

proof_coverage_linest &proof_coverage_checkert::branch_lines(
  const proof_coverage_linet &decision,
  bool polarity)
{
  const auto entry = branch_index.emplace(
    std::make_pair(decision, polarity), program.branches.size());
  if(entry.second)
    program.branches.push_back(proof_coverage_brancht{decision, polarity, {}});
  return program.branches[entry.first->second].lines;
}

void proof_coverage_checkert::collect_coverable_lines()
{
  for(const auto &function : goto_model.get_goto_functions().function_map)
  {
    if(id2string(function.first).rfind("__CPROVER", 0) == 0)
      continue;

    for(const auto &instruction : function.second.body.instructions)
    {
      const auto line =
        proof_coverage_user_line(instruction.source_location());
      if(!line.has_value())
        continue;

      const bool is_statement =
        instruction.is_assign() || instruction.is_function_call() ||
        (instruction.is_goto() && !instruction.condition().is_true()) ||
        (instruction.is_assert() &&
         instruction.source_location().get_property_class() == "assertion");

      if(is_statement)
        program.coverable_lines.insert(*line);
    }
  }
}

exprt proof_coverage_checkert::undef(const exprt &expr)
{
  if(expr.id() != ID_symbol)
    return false_exprt();

  return symbol_exprt(
    id2string(to_symbol_expr(expr).get_identifier()) + "$undef", bool_typet());
}

exprt proof_coverage_checkert::any_undef(const exprt &condition) const
{
  // Guard symbols stand for the branch conditions; the variables those
  // conditions read are the ones that can make the phi undefined.
  std::set<irep_idt> seen;
  exprt::operandst disjuncts;
  const auto add = [&](const symbol_exprt &symbol) {
    if(seen.insert(symbol.get_identifier()).second)
      disjuncts.push_back(undef(symbol));
  };

  for(const auto &symbol : find_symbols(condition))
  {
    const auto guard = guards.find(symbol.get_identifier());
    if(guard == guards.end())
      add(symbol);
    else
    {
      for(const auto &read : find_symbols(guard->second.condition))
        add(read);
    }
  }

  return disjunction(disjuncts);
}
