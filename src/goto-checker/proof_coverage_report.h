/*******************************************************************\

Module: Proof Coverage - Reporting

Author: proofcov

\*******************************************************************/

/// \file
/// Line and branch coverage reports for proof cores

#ifndef CPROVER_GOTO_CHECKER_PROOF_COVERAGE_REPORT_H
#define CPROVER_GOTO_CHECKER_PROOF_COVERAGE_REPORT_H

#include <util/irep.h>

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

class optionst;
class source_locationt;
class ui_message_handlert;

/// A line in a user source file
struct proof_coverage_linet
{
  irep_idt file;
  std::size_t line;
  /// Absolute path of the file, if known, for printing its contents
  std::string full_path;

  bool operator<(const proof_coverage_linet &other) const
  {
    return std::tie(file, line) < std::tie(other.file, other.line);
  }
};

using proof_coverage_linest = std::set<proof_coverage_linet>;

/// \return the line of \p source_location if it belongs to user code,
///   i.e., is neither hidden, built-in nor part of the CPROVER library
std::optional<proof_coverage_linet>
proof_coverage_user_line(const source_locationt &source_location);

/// One outcome (then/else) of a branching condition
struct proof_coverage_brancht
{
  /// The line of the condition
  proof_coverage_linet decision;
  /// true for the branch taken when the condition holds
  bool polarity;
  /// The statements in this branch
  proof_coverage_linest lines;
};

/// Static information about the program needed to compute coverage
struct proof_coverage_programt
{
  /// All lines that can be covered (the denominator of line coverage)
  proof_coverage_linest coverable_lines;
  /// The lines of the checked assertions, which every proof uses
  proof_coverage_linest assertion_lines;
  /// The branches that contain at least one statement
  std::vector<proof_coverage_brancht> branches;
};

/// A proof core: a minimal set of soft constraints that, together with
/// the hard constraints, suffices to prove the assertions
struct proof_coverage_coret
{
  /// Names of the soft constraints, in the style of the Python prototype,
  /// e.g. `line5.0` or `phi.if.7.b!0@1#4`
  std::vector<std::string> constraint_names;
  /// Lines used by the soft constraints in the core
  proof_coverage_linest lines;
  /// With --proof-coverage-trace: "trace" (everything the core uses was
  /// executed by the run), "non-trace" or "unknown"; empty otherwise
  std::string trace;
  /// For a non-trace core: what it uses that the run did not execute
  std::string not_executed;
};

class proof_coverage_reportt
{
public:
  proof_coverage_reportt(
    const optionst &options,
    ui_message_handlert &ui_message_handler,
    const proof_coverage_programt &program);

  /// Reports the coverage of a proof using \p core
  void operator()(std::size_t core_number, const proof_coverage_coret &core);

protected:
  const optionst &options;
  ui_message_handlert &ui_message_handler;
  const proof_coverage_programt &program;

  void report_plain(
    std::size_t core_number,
    const proof_coverage_coret &core,
    const proof_coverage_linest &covered);

  void report_json(
    std::size_t core_number,
    const proof_coverage_coret &core,
    const proof_coverage_linest &covered);

  void annotate_source(const proof_coverage_linest &covered);
};

#endif // CPROVER_GOTO_CHECKER_PROOF_COVERAGE_REPORT_H
