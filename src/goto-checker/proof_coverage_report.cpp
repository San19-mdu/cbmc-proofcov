/*******************************************************************\

Module: Proof Coverage - Reporting

Author: proofcov

\*******************************************************************/

/// \file
/// Line and branch coverage reports for proof cores

#include "proof_coverage_report.h"

#include <util/json.h>
#include <util/json_stream.h>
#include <util/options.h>
#include <util/source_location.h>
#include <util/string2int.h>
#include <util/ui_message.h>

#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

static jsont json_bool(bool value)
{
  if(value)
    return json_truet();
  return json_falset();
}

std::optional<proof_coverage_linet>
proof_coverage_user_line(const source_locationt &source_location)
{
  if(source_location.is_nil() || source_location.get_hide())
    return {};

  const std::string &file = id2string(source_location.get_file());
  if(file.empty() || file[0] == '<' || source_location.is_built_in())
    return {};

  const std::string &function = id2string(source_location.get_function());
  if(function.rfind("__CPROVER", 0) == 0)
    return {};

  const auto line = string2optional_size_t(id2string(source_location.get_line()));
  if(!line.has_value())
    return {};

  return proof_coverage_linet{
    source_location.get_file(),
    *line,
    source_location.full_path().value_or(file)};
}

proof_coverage_reportt::proof_coverage_reportt(
  const optionst &options,
  ui_message_handlert &ui_message_handler,
  const proof_coverage_programt &program)
  : options(options), ui_message_handler(ui_message_handler), program(program)
{
}

static std::string percentage(std::size_t covered, std::size_t total)
{
  std::ostringstream out;
  out << covered << '/' << total << " (" << std::fixed
      << std::setprecision(2)
      << (total == 0 ? 100.0 : 100.0 * covered / total) << "%)";
  return out.str();
}

static bool is_branch_covered(
  const proof_coverage_brancht &branch,
  const proof_coverage_linest &covered)
{
  for(const auto &line : branch.lines)
  {
    if(covered.find(line) != covered.end())
      return true;
  }
  return false;
}

void proof_coverage_reportt::operator()(
  std::size_t core_number,
  const proof_coverage_coret &core)
{
  // Every proof uses the assertions.
  proof_coverage_linest covered = core.lines;
  covered.insert(program.assertion_lines.begin(), program.assertion_lines.end());

  if(ui_message_handler.get_ui() == ui_message_handlert::uit::JSON_UI)
    report_json(core_number, core, covered);
  else
    report_plain(core_number, core, covered);
}

void proof_coverage_reportt::report_plain(
  std::size_t core_number,
  const proof_coverage_coret &core,
  const proof_coverage_linest &covered)
{
  messaget log(ui_message_handler);

  if(options.get_bool_option("proof-coverage-experiment"))
  {
    // the format expected by tcas/experiment.py
    std::ostringstream lines;
    for(const auto &line : covered)
      lines << ' ' << line.line;
    log.result() << "COVERED:" << lines.str() << messaget::eom;
    if(!core.trace.empty())
    {
      log.result() << "TRACE: " << core.trace
                   << (core.not_executed.empty() ? "" : " " + core.not_executed)
                   << messaget::eom;
    }
    return;
  }

  log.result() << "\n** Proof core " << core_number << " ("
               << core.constraint_names.size() << " soft constraints)"
               << messaget::eom;

  if(options.get_bool_option("proof-coverage-annotate"))
  {
    std::ostringstream names;
    for(const auto &name : core.constraint_names)
      names << ' ' << name;
    log.result() << "Core:" << names.str() << messaget::eom;
    annotate_source(covered);
  }

  std::ostringstream covered_lines;
  for(const auto &line : covered)
    covered_lines << ' ' << line.file << ':' << line.line;
  log.result() << "Covered lines:" << covered_lines.str() << messaget::eom;
  if(!core.trace.empty())
  {
    log.result() << "Trace: " << core.trace
                 << (core.not_executed.empty()
                       ? ""
                       : " (not executed by the run: " + core.not_executed + ")")
                 << messaget::eom;
  }

  std::size_t covered_coverable = 0;
  for(const auto &line : program.coverable_lines)
  {
    if(covered.find(line) != covered.end())
      ++covered_coverable;
  }
  log.result() << "Line coverage: "
               << percentage(covered_coverable, program.coverable_lines.size())
               << messaget::eom;

  if(options.get_bool_option("proof-coverage-branch"))
  {
    std::size_t covered_branches = 0;
    for(const auto &branch : program.branches)
    {
      const bool is_covered = is_branch_covered(branch, covered);
      if(is_covered)
        ++covered_branches;

      if(options.get_bool_option("proof-coverage-annotate"))
      {
        log.result() << "  Branch at line " << branch.decision.line << " ("
                     << (branch.polarity ? "true" : "false") << "): "
                     << (is_covered ? messaget::green : messaget::red)
                     << (is_covered ? "covered" : "not covered")
                     << messaget::reset << messaget::eom;
      }
    }
    log.result() << "Branch coverage: "
                 << percentage(covered_branches, program.branches.size())
                 << messaget::eom;
  }
}

void proof_coverage_reportt::annotate_source(
  const proof_coverage_linest &covered)
{
  messaget log(ui_message_handler);

  std::map<irep_idt, std::string> files;
  for(const auto &line : program.coverable_lines)
    files.emplace(line.file, line.full_path);

  for(const auto &file : files)
  {
    std::ifstream in(file.second);
    if(!in)
    {
      log.warning() << "cannot open " << file.second << messaget::eom;
      continue;
    }

    log.result() << "File " << file.first << ':' << messaget::eom;
    std::string text;
    for(std::size_t line_number = 1; std::getline(in, text); ++line_number)
    {
      const proof_coverage_linet line{file.first, line_number, ""};
      const bool is_covered = covered.find(line) != covered.end();
      const bool is_coverable = program.coverable_lines.find(line) !=
                                program.coverable_lines.end();

      std::ostringstream number;
      number << std::setw(3) << std::setfill('0') << line_number;

      if(is_covered)
      {
        log.result() << number.str() << " + " << messaget::green << text
                     << messaget::reset << messaget::eom;
      }
      else if(is_coverable)
      {
        log.result() << number.str() << " - " << messaget::red << text
                     << messaget::reset << messaget::eom;
      }
      else
        log.result() << number.str() << "   " << text << messaget::eom;
    }
  }
}

static json_objectt json(const proof_coverage_linet &line)
{
  return json_objectt{
    {"file", json_stringt{line.file}},
    {"line", json_numbert{std::to_string(line.line)}}};
}

void proof_coverage_reportt::report_json(
  std::size_t core_number,
  const proof_coverage_coret &core,
  const proof_coverage_linest &covered)
{
  json_arrayt constraints;
  for(const auto &name : core.constraint_names)
    constraints.push_back(json_stringt{name});

  json_arrayt lines;
  std::size_t covered_coverable = 0;
  for(const auto &line : program.coverable_lines)
  {
    const bool is_covered = covered.find(line) != covered.end();
    if(is_covered)
      ++covered_coverable;
    json_objectt entry = json(line);
    entry["covered"] = json_bool(is_covered);
    lines.push_back(std::move(entry));
  }

  json_objectt result{
    {"core", json_numbert{std::to_string(core_number)}},
    {"constraints", std::move(constraints)},
    {"lines", std::move(lines)},
    {"lineCoverage",
     json_objectt{
       {"covered", json_numbert{std::to_string(covered_coverable)}},
       {"total",
        json_numbert{std::to_string(program.coverable_lines.size())}}}}};

  if(options.get_bool_option("proof-coverage-branch"))
  {
    json_arrayt branches;
    std::size_t covered_branches = 0;
    for(const auto &branch : program.branches)
    {
      const bool is_covered = is_branch_covered(branch, covered);
      if(is_covered)
        ++covered_branches;
      json_objectt entry = json(branch.decision);
      entry["polarity"] = json_bool(branch.polarity);
      entry["covered"] = json_bool(is_covered);
      branches.push_back(std::move(entry));
    }
    result["branches"] = std::move(branches);
    result["branchCoverage"] = json_objectt{
      {"covered", json_numbert{std::to_string(covered_branches)}},
      {"total", json_numbert{std::to_string(program.branches.size())}}};
  }

  json_objectt wrapper{{"proofCoverage", std::move(result)}};
  ui_message_handler.get_json_stream().push_back(wrapper);
}
