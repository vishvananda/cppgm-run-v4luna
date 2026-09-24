// Student-facing scaffold for the PA5+ `cppgm++` binary.

#include "support/not_implemented.h"
#include "support/tool_help_text.h"
#include "parser/ast_parser.h"
#include "semantic/pa6.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace {

enum class EmitMode
{
  None,
  Ast,
  Types,
  Semantics,
  LowIR,
};

enum class DriverMode
{
  Query,
  Preprocess,
  Compile,
  Link,
};

struct DriverInvocation
{
  DriverMode mode;

  DriverInvocation()
      : mode(DriverMode::Link)
  {
  }
};

vector<string> collect_args(int argc, char ** argv)
{
  vector<string> args;
  for(int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

bool has_arg(const vector<string> & args, const string & needle)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == needle) {
      return true;
    }
  }
  return false;
}

bool has_help_arg(const vector<string> & args)
{
  return has_arg(args, "--help") || has_arg(args, "-h");
}

bool starts_with(const string & value, const string & prefix)
{
  return value.size() >= prefix.size() &&
      value.compare(0, prefix.size(), prefix) == 0;
}

bool is_query_driver_flag(const string & arg)
{
  return arg == "--version" ||
      arg == "-v" ||
      arg == "-dumpmachine" ||
      arg == "-dumpversion" ||
      arg == "-print-search-dirs";
}

bool is_optimization_flag(const string & arg)
{
  return starts_with(arg, "-O");
}

bool is_debug_info_flag(const string & arg)
{
  return arg == "-g0" ||
      arg == "-gline-tables-only" ||
      arg == "-g" ||
      starts_with(arg, "-g");
}

bool is_benign_driver_flag(const string & arg)
{
  return arg == "-Wall" ||
      arg == "-Winvalid-offsetof" ||
      arg == "-pipe" ||
      arg == "-w" ||
      arg == "-pg" ||
      arg == "-pedantic" ||
      arg == "-pedantic-errors" ||
      starts_with(arg, "-W") ||
      starts_with(arg, "-f") ||
      starts_with(arg, "-m") ||
      starts_with(arg, "-std=");
}

logic_error missing_option_argument(const string & option,
                                    const string & expected)
{
  return logic_error("missing " + expected + " after " + option);
}

void consume_required_option_argument(const vector<string> & args,
                                      size_t & i,
                                      const string & option,
                                      const string & expected)
{
  if(i + 1 >= args.size()) {
    throw missing_option_argument(option, expected);
  }
  ++i;
}

bool consume_joined_or_separate_option(const vector<string> & args,
                                       size_t & i,
                                       const string & option,
                                       const string & expected)
{
  if(args[i] == option) {
    consume_required_option_argument(args, i, option, expected);
    return true;
  }
  if(starts_with(args[i], option) && args[i].size() > option.size()) {
    return true;
  }
  return false;
}

int run_emit_ast_mode(const vector<string> & args);

vector<string> split_batch_fields(const string & line)
{
  vector<string> fields;
  size_t start = 0;
  for(;;) {
    size_t tab = line.find('\t', start);
    fields.push_back(line.substr(start, tab == string::npos ? string::npos : tab - start));
    if(tab == string::npos) break;
    start = tab + 1;
  }
  return fields;
}

int run_batch_stdin(const vector<string> & raw_args)
{
  vector<string> base_args;
  for(size_t i = 0; i < raw_args.size(); ++i) {
    if(raw_args[i] != "--batch-stdin" && raw_args[i] != "--emit-ast")
      base_args.push_back(raw_args[i]);
  }
  string line;
  while(getline(cin, line)) {
    if(!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    if(line.empty()) continue;
    vector<string> fields = split_batch_fields(line);
    vector<string> args = base_args;
    string error_path;
    if(fields.size() == 3) {
      error_path = fields[1];
      args.push_back("-o"); args.push_back(fields[0]); args.push_back(fields[2]);
    } else if(fields.size() >= 5) {
      error_path = fields[1];
      for(size_t i = 4; i < fields.size(); ++i) args.push_back(fields[i]);
    } else {
      cout << "EXIT_FAILURE" << endl;
      continue;
    }
    ofstream error_file(error_path.c_str(), ios::out | ios::trunc);
    streambuf * old_error = cerr.rdbuf();
    if(error_file) cerr.rdbuf(error_file.rdbuf());
    int status = EXIT_FAILURE;
    try { status = run_emit_ast_mode(args); }
    catch(const exception & e) { cerr << "ERROR: " << e.what() << endl; }
    cerr.flush();
    cerr.rdbuf(old_error);
    if(error_file) error_file.close();
    cout << (status == EXIT_SUCCESS ? "EXIT_SUCCESS" : "EXIT_FAILURE") << endl;
  }
  return EXIT_SUCCESS;
}

void consume_emit_flag(vector<string> & args,
                       const string & flag,
                       EmitMode value,
                       EmitMode & out)
{
  vector<string> kept;
  bool found = false;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == flag) {
      found = true;
      continue;
    }
    kept.push_back(args[i]);
  }

  if(!found) {
    return;
  }

  if(out != EmitMode::None) {
    throw logic_error("multiple --emit-* options provided");
  }
  out = value;
  args.swap(kept);
}

EmitMode parse_emit_mode(vector<string> & args)
{
  EmitMode mode = EmitMode::None;
  consume_emit_flag(args, "--emit-ast", EmitMode::Ast, mode);
  consume_emit_flag(args, "--emit-types", EmitMode::Types, mode);
  consume_emit_flag(args, "--emit-semantics", EmitMode::Semantics, mode);
  consume_emit_flag(args, "--emit-lowir", EmitMode::LowIR, mode);
  return mode;
}

void parse_source_output_invocation(const vector<string> & args,
                                    bool allow_lowir_options)
{
  bool explicit_outfile = false;
  vector<string> inputs;

  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      explicit_outfile = true;
      continue;
    }
    if(allow_lowir_options &&
       (is_optimization_flag(args[i]) || is_debug_info_flag(args[i]))) {
      continue;
    }
    if(allow_lowir_options &&
       (args[i] == "--witness" || args[i] == "--witness-debug")) {
      consume_required_option_argument(args, i, args[i], "output file");
      continue;
    }
    if(args[i] == "-c" || args[i] == "-E" || is_query_driver_flag(args[i])) {
      throw logic_error("invalid usage");
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported option in emit mode: " + args[i]);
    }
    inputs.push_back(args[i]);
  }

  if(!explicit_outfile || inputs.empty()) {
    throw logic_error("invalid usage");
  }
}

bool consume_preprocess_option(const vector<string> & args, size_t & i)
{
  if(consume_joined_or_separate_option(args, i, "-D", "macro definition")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-U", "macro name")) {
    return true;
  }
  if(args[i] == "-include") {
    consume_required_option_argument(args, i, "-include", "file");
    return true;
  }
  return false;
}

bool consume_search_option(const vector<string> & args, size_t & i)
{
  if(consume_joined_or_separate_option(args, i, "-I", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-isystem", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-L", "path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-l", "library name")) {
    return true;
  }
  return false;
}

bool consume_dependency_option(const vector<string> & args, size_t & i)
{
  if(args[i] == "-MMD" || args[i] == "-MD" || args[i] == "-MP") {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MF", "depfile path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MT", "target")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MQ", "target")) {
    return true;
  }
  return false;
}

bool consume_toolchain_option(const vector<string> & args, size_t & i)
{
  if(is_debug_info_flag(args[i])) {
    return true;
  }
  if(is_optimization_flag(args[i])) {
    return true;
  }
  if(args[i] == "--target") {
    consume_required_option_argument(args, i, "--target", "target");
    return true;
  }
  if(starts_with(args[i], "--target=")) {
    if(args[i].size() == string("--target=").size()) {
      throw missing_option_argument("--target", "target");
    }
    return true;
  }
  if(args[i] == "-std") {
    consume_required_option_argument(args, i, "-std", "language standard");
    return true;
  }
  if(args[i] == "-stdlib") {
    consume_required_option_argument(args, i, "-stdlib", "standard library name");
    return true;
  }
  if(starts_with(args[i], "-stdlib=")) {
    return true;
  }
  if(args[i] == "-pthread") {
    throw logic_error("option not yet supported: -pthread");
  }
  return false;
}

DriverInvocation parse_driver_invocation(const vector<string> & args)
{
  if(args.empty()) {
    throw logic_error("invalid usage");
  }

  DriverInvocation invocation;
  if(is_query_driver_flag(args[0])) {
    if(args.size() != 1) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    invocation.mode = DriverMode::Query;
    return invocation;
  }

  bool compile_only = false;
  bool preprocess_only = false;
  bool explicit_outfile = false;
  vector<string> inputs;

  for(size_t i = 0; i < args.size(); ++i) {
    if(is_query_driver_flag(args[i])) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    if(args[i] == "-c") {
      compile_only = true;
      continue;
    }
    if(args[i] == "-E") {
      preprocess_only = true;
      continue;
    }
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      explicit_outfile = true;
      continue;
    }
    if(consume_preprocess_option(args, i) ||
       consume_search_option(args, i) ||
       consume_dependency_option(args, i) ||
       consume_toolchain_option(args, i) ||
       is_benign_driver_flag(args[i])) {
      continue;
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported driver option: " + args[i]);
    }
    inputs.push_back(args[i]);
  }

  if(compile_only && preprocess_only) {
    throw logic_error("cannot combine -c and -E");
  }
  if(inputs.empty()) {
    throw logic_error("invalid usage");
  }
  if((compile_only || preprocess_only) && explicit_outfile && inputs.size() != 1) {
    throw logic_error("cannot specify -o when generating multiple output files");
  }

  invocation.mode =
      preprocess_only ? DriverMode::Preprocess :
      compile_only ? DriverMode::Compile :
      DriverMode::Link;
  return invocation;
}

int run_unimplemented_mode(const char * feature,
                           const char * owner)
{
  (void)feature;
  (void)owner;
  throw NotImplementedException();
}

int run_emit_ast_mode(const vector<string> & args)
{
  parse_source_output_invocation(args, false);
  string output;
  vector<string> inputs;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "-o") {
      if(i + 1 >= args.size()) throw logic_error("missing output file after -o");
      output = args[++i];
    } else {
      inputs.push_back(args[i]);
    }
  }
  cppgm::EmitAst(inputs, output);
  return EXIT_SUCCESS;
}

int run_emit_types_mode(const vector<string> & args)
{
  parse_source_output_invocation(args, false);
  string output;
  vector<string> inputs;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "-o") {
      if(i + 1 >= args.size()) throw logic_error("missing output file after -o");
      output = args[++i];
    } else {
      inputs.push_back(args[i]);
    }
  }
  cppgm::EmitTypes(inputs, output);
  return EXIT_SUCCESS;
}

int run_emit_semantics_mode(const vector<string> & args)
{
  parse_source_output_invocation(args, false);
  return run_unimplemented_mode("--emit-semantics", "PA7");
}

int run_emit_lowir_mode(const vector<string> & args)
{
  parse_source_output_invocation(args, true);
  return run_unimplemented_mode("--emit-lowir", "PA9");
}

int run_driver_mode(const vector<string> & args)
{
  const DriverInvocation invocation = parse_driver_invocation(args);
  switch(invocation.mode) {
  case DriverMode::Query:
    return run_unimplemented_mode("driver query mode", "PA29");
  case DriverMode::Preprocess:
    return run_unimplemented_mode("hosted preprocess driver mode (-E)", "PA29");
  case DriverMode::Compile:
    return run_unimplemented_mode("compile driver mode (-c)", "PA24");
  case DriverMode::Link:
    return run_unimplemented_mode("link driver mode", "PA24");
  }
  throw logic_error("unreachable driver mode");
}

int run_cppgm(const vector<string> & raw_args)
{
  if(has_arg(raw_args, "--batch-stdin")) {
    return run_batch_stdin(raw_args);
  }

  if(has_help_arg(raw_args)) {
    cout << cppgm_help_text();
    return EXIT_SUCCESS;
  }

  vector<string> args = raw_args;
  const EmitMode mode = parse_emit_mode(args);

  switch(mode) {
  case EmitMode::Ast:
    return run_emit_ast_mode(args);
  case EmitMode::Types:
    return run_emit_types_mode(args);
  case EmitMode::Semantics:
    return run_emit_semantics_mode(args);
  case EmitMode::LowIR:
    return run_emit_lowir_mode(args);
  case EmitMode::None:
    return run_driver_mode(args);
  }

  throw logic_error("unreachable emit mode");
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_cppgm(collect_args(argc, argv));
  }
  catch(const NotImplementedException & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return CPPGM_EXIT_NOT_IMPLEMENTED;
  }
  catch(const exception & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
}
