/*******************************************************************\

Module: Verilog Indexer

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

#include "verilog_indexer.h"

#include <util/cmdline.h>
#include <util/cout_message.h>
#include <util/suffix.h>
#include <util/unicode.h>

#include <verilog/verilog_parser.h>
#include <verilog/verilog_preprocessor.h>
#include <verilog/verilog_y.tab.h>

#include <algorithm>
#include <fstream>
#include <iosfwd>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

std::size_t verilog_indexert::total_number_of_files() const
{
  return file_map.size();
}

std::map<verilog_indexert::idt::kindt, std::size_t>
verilog_indexert::total_number_of() const
{
  std::map<verilog_indexert::idt::kindt, std::size_t> result;
  for(auto &[_, file] : file_map)
    for(auto &id : file.ids)
      result[id.kind]++;
  return result;
}

std::size_t verilog_indexert::total_number_of_lines() const
{
  std::size_t sum = 0;
  for(auto &[_, file] : file_map)
    sum += file.number_of_lines;
  return sum;
}

class verilog_indexer_parsert
{
public:
  explicit verilog_indexer_parsert(verilog_indexert &__indexer)
    : indexer(__indexer)
  {
  }

  // grammar rules
  void rDescription();

protected:
  using idt = verilog_indexert::idt;
  verilog_indexert &indexer;
  irep_idt current_module;

  // modules, classes, primitives, packages, interfaces
  void rModule(verilog_indexert::idt::kindt, int end_token);
  void rImport();
  void rExtends();
  void rPorts();
  void rItem();

  void rConstruct(); // always, initial, ...
  void rStatement();
  void rBegin();
  void rAssertAssumeCover();
  void rFor();
  void rForEver();
  void rWhile();
  void rIf();
  void rCase();
  void rCaseLabel();
  void rUniquePriority();
  void rParenExpression(); // (expression)
  void rDeclaration();     // var, reg, wire, input, typedef, defparam ...
  void rType();
  void rTypeOpt();
  void rStructUnion();
  void rEnum();
  void rDeclarators();
  void rTaskFunction();     // task ... endtask
  void rContinuousAssign(); // assign
  void rGenerate();         // generate ... endgenerate
  void rGenerateFor();
  void rGenerateIf();
  void rGenerateBegin();
  void rModuleInstance();
  void rLabeledItem();
  void rProperty(); // assert, assume, cover
  void rClocking();
  void rCoverGroup();
  void skip_until(int token);

  struct tokent
  {
    int kind;
    std::string text;
    bool is_eof() const
    {
      return kind == 0; // EOF, flex magic number
    }
    bool is_identifier() const
    {
      return kind == TOK_NON_TYPE_IDENTIFIER || kind == TOK_TYPE_IDENTIFIER;
    }
    bool is_system_identifier() const
    {
      return kind == TOK_SYSIDENT;
    }
    bool operator==(int other) const
    {
      return kind == other;
    }
    bool operator!=(int other) const
    {
      return kind != other;
    }
  };

  const tokent &peek()
  {
    return peek1();
  }

  const tokent &next_token();
  const tokent &peek1();
  const tokent &peek2();

  tokent peeked_token, extra_token;
  bool have_peeked_token = false, have_extra_token = false;

  static tokent fetch_token();
};

void verilog_indexert::operator()(
  const irep_idt &file_name,
  enum VerilogStandardt standard)
{
  // Is it SystemVerilog?
  using standardt = VerilogStandardt;
  if(
    has_suffix(id2string(file_name), ".sv") || standard == standardt::SV2017 ||
    standard == standardt::SV2012 || standard == standardt::SV2009 ||
    standard == standardt::SV2005)
    verilog_parser.mode = verilog_parsert::SYSTEM_VERILOG;
  else
    verilog_parser.mode = verilog_parsert::VIS_VERILOG;

  // run the preprocessor
  const auto preprocessed_string = preprocess(id2string(file_name));
  std::istringstream preprocessed(preprocessed_string);

  // set up the tokenizer
  verilog_parser.in = &preprocessed;
  verilog_scanner_init();

  // now parse
  verilog_indexer_parsert parser(*this);
  parser.rDescription();
  file_map[file_name].number_of_lines = verilog_parser.get_line_no();
}

std::string verilog_indexert::preprocess(const std::string &file_name)
{
  std::stringstream preprocessed;

  auto in_stream = std::ifstream(widen_if_needed(file_name));

  if(!in_stream)
  {
    // We deliberately fail silently.
    // Errors on invalid file names are expected to be raised
    // later.
    return std::string();
  }

  console_message_handlert message_handler;
  verilog_preprocessort preprocessor(
    in_stream, preprocessed, message_handler, file_name);

  try
  {
    preprocessor.preprocessor();
  }
  catch(...)
  {
  }

  return preprocessed.str();
}

const verilog_indexer_parsert::tokent &verilog_indexer_parsert::next_token()
{
  peek();
  have_peeked_token = false;
  return peeked_token;
}

// scanner interface
int yyveriloglex();
extern char *yyverilogtext;
extern int yyverilogleng;

verilog_indexer_parsert::tokent verilog_indexer_parsert::fetch_token()
{
  tokent result;
  result.kind = yyveriloglex();
  result.text = std::string(yyverilogtext, yyverilogleng);
  return result;
}

const verilog_indexer_parsert::tokent &verilog_indexer_parsert::peek1()
{
  if(!have_peeked_token)
  {
    if(have_extra_token)
    {
      peeked_token = std::move(extra_token);
      have_extra_token = false;
      have_peeked_token = true;
    }
    else
    {
      // no token available
      peeked_token = fetch_token();
      have_peeked_token = true;
    }
  }

  return peeked_token;
}

const verilog_indexer_parsert::tokent &verilog_indexer_parsert::peek2()
{
  peek1();

  if(!have_extra_token)
  {
    // Only one token available, but we want two.
    extra_token = fetch_token();
    have_extra_token = true;
  }

  return extra_token;
}

void verilog_indexer_parsert::rDescription()
{
  if(peek().is_eof())
    return; // empty file

  if(next_token().kind != TOK_PARSE_LANGUAGE)
    DATA_INVARIANT(false, "expected TOK_PARSE_LANGUAGE");

  while(!peek().is_eof())
  {
    rItem();
  }
}

/// Covers the various 'definition-like' constructs in SystemVerilog, i.e.,
/// modules, interfaces, classes, primitives, packages
void verilog_indexer_parsert::rModule(
  verilog_indexert::idt::kindt kind,
  int end_token)
{
  auto keyword = next_token(); // module, ...

  auto name = next_token();
  if(!name.is_identifier())
    return; // give up

  current_module = name.text;

  idt id;
  id.kind = kind;
  id.name = current_module;
  id.module = current_module;
  id.file_name = verilog_parser.get_file();
  id.line_number = verilog_parser.get_line_no();
  indexer.add(std::move(id));

  if(peek() == TOK_IMPORT)
    rImport();

  if(peek() == TOK_EXTENDS)
    rExtends();

  rPorts();

  // now look for the 'endmodule', given as end_token
  while(!peek().is_eof())
  {
    if(peek() == end_token)
    {
      next_token();
      break; // done with the module
    }
    else
      rItem();
  }

  // optional : name
  if(peek() == ':')
  {
    next_token(); // :
    next_token(); // identifier
  }

  current_module = irep_idt();
}

void verilog_indexer_parsert::rItem()
{
  auto &token = peek();

  if(token == TOK_MODULE)
    rModule(verilog_indexert::idt::MODULE, TOK_ENDMODULE);
  else if(token == TOK_CLASS)
    rModule(verilog_indexert::idt::CLASS, TOK_ENDCLASS);
  else if(token == TOK_PRIMITIVE)
    rModule(verilog_indexert::idt::UDP, TOK_ENDPRIMITIVE);
  else if(token == TOK_INTERFACE)
    rModule(verilog_indexert::idt::INTERFACE, TOK_ENDINTERFACE);
  else if(token == TOK_PACKAGE)
    rModule(verilog_indexert::idt::PACKAGE, TOK_ENDPACKAGE);
  else if(
    token == TOK_ALWAYS || token == TOK_ALWAYS_FF || token == TOK_ALWAYS_COMB ||
    token == TOK_ALWAYS_LATCH || token == TOK_FINAL || token == TOK_INITIAL)
  {
    rConstruct();
  }
  else if(token == TOK_DEFAULT)
  {
    rClocking();
  }
  else if(token == TOK_COVERGROUP)
  {
    rCoverGroup();
  }
  else if(
    token == TOK_VAR || token == TOK_WIRE || token == TOK_TRI0 ||
    token == TOK_TRI1 || token == TOK_REG || token == TOK_INPUT ||
    token == TOK_INOUT || token == TOK_OUTPUT || token == TOK_GENVAR ||
    token == TOK_TYPEDEF || token == TOK_ENUM || token == TOK_PARAMETER ||
    token == TOK_LOCALPARAM || token == TOK_DEFPARAM || token == TOK_SUPPLY0 ||
    token == TOK_SUPPLY1 || token == TOK_INTEGER || token == TOK_REALTIME ||
    token == TOK_REAL || token == TOK_SHORTREAL || token == TOK_BYTE ||
    token == TOK_SHORTINT || token == TOK_LOGIC || token == TOK_BIT ||
    token == TOK_LET || token == TOK_INT || token == TOK_STRUCT ||
    token == TOK_UNION || token == TOK_RAND || token == TOK_LOCAL)
  {
    rDeclaration();
  }
  else if(token == TOK_FUNCTION || token == TOK_TASK || token == TOK_VIRTUAL)
    rTaskFunction();
  else if(token == TOK_ASSIGN)
    rContinuousAssign();
  else if(token == TOK_IF)
    rGenerateIf();
  else if(token == TOK_BEGIN)
    rGenerateBegin();
  else if(token == TOK_FOR)
    rGenerateFor();
  else if(token == TOK_GENERATE)
    rGenerate();
  else if(token == TOK_ASSERT || token == TOK_ASSUME || token == TOK_COVER)
    rProperty();
  else if(
    token == TOK_BUF || token == TOK_OR || token == TOK_NOR ||
    token == TOK_XOR || token == TOK_XNOR || token == TOK_AND ||
    token == TOK_NAND || token == TOK_NOT || token == TOK_BUFIF0 ||
    token == TOK_BUFIF1)
  {
    rModuleInstance();
  }
  else if(token.is_identifier())
  {
    // Type identifier (for declaration), label, module instance.
    // We look one further token ahead to disambiguate.
    auto &second_token = peek2();
    if(second_token == '#' || second_token == '(')
      rModuleInstance();
    else if(second_token == TOK_COLON)
      rLabeledItem();
    else
      rDeclaration();
  }
  else if(token == TOK_TIMEUNIT)
  {
    skip_until(';');
  }
  else if(token == TOK_TIMEPRECISION)
  {
    skip_until(';');
  }
  else if(token == ';')
  {
    // the empty item
    next_token();
  }
  else if(token.is_system_identifier())
  {
    // $error...
    skip_until(';');
  }
  else if(token == TOK_IMPORT)
  {
    skip_until(';');
  }
  else if(token == '(')
  {
    // possibly a macro that wasn't found
    std::cout << "LPAREN: " << verilog_parser.get_file() << ':'
              << verilog_parser.get_line_no() << ' ' << token.text << "\n";
    rParenExpression();
  }
  else
  {
    // something else
    std::cout << "ELSE: " << verilog_parser.get_file() << ':'
              << verilog_parser.get_line_no() << ' ' << token.text << "\n";
    next_token();
  }
}

void verilog_indexer_parsert::rImport()
{
  next_token(); // import
  skip_until(';');
}

void verilog_indexer_parsert::rExtends()
{
  next_token(); // extends
  next_token(); // identifier
}

void verilog_indexer_parsert::rPorts()
{
  skip_until(';');
}

void verilog_indexer_parsert::rConstruct()
{
  auto keyword = next_token(); // initial, final, always, always_comb, ...
  rStatement();
}

void verilog_indexer_parsert::rClocking()
{
  next_token(); // default
  if(next_token() != TOK_CLOCKING)
    return;
  skip_until(TOK_ENDCLOCKING);
}

void verilog_indexer_parsert::rCoverGroup()
{
  next_token(); // covergroup
  skip_until(TOK_ENDGROUP);
}

void verilog_indexer_parsert::rStatement()
{
  auto first = peek();
  if(first == TOK_ASSERT || first == TOK_ASSUME || first == TOK_COVER)
    rAssertAssumeCover();
  else if(first == TOK_BEGIN)
    rBegin();
  else if(first == TOK_CASE || first == TOK_CASEX || first == TOK_CASEZ)
    rCase();
  else if(first == TOK_UNIQUE || first == TOK_UNIQUE0 || first == TOK_PRIORITY)
    rUniquePriority();
  else if(first == TOK_FOR)
    rFor();
  else if(first == TOK_FOREVER)
    rForEver();
  else if(first == TOK_WHILE)
    rWhile();
  else if(first == TOK_IF)
    rIf();
  else if(first == '@')
  {
    next_token(); // @
    if(peek() == '(')
      rParenExpression();
    else if(peek() == TOK_PARENASTERIC) // (*
    {
      next_token();
      if(peek() == ')')
        next_token();
    }
    else
      next_token();

    rStatement();
  }
  else if(first == '#')
  {
    // delay
    next_token(); // #
    next_token(); // delay value
    rStatement();
  }
  else if(first == ';')
  {
    // skip
    next_token();
  }
  else
  {
    // Label?
    if(first.is_identifier() && peek2() == TOK_COLON)
    {
      next_token(); // identifier
      next_token(); // :
      rStatement();
    }
    else
    {
      // e.g., declarations, assignments, ...
      skip_until(';');
    }
  }
}

void verilog_indexer_parsert::rAssertAssumeCover()
{
  next_token(); // assert, assume, ...
  rParenExpression();
  if(peek() == TOK_ELSE)
  {
    next_token(); // else
    rStatement();
  }
  else
    skip_until(';');
}

void verilog_indexer_parsert::rBegin()
{
  next_token(); // begin

  if(peek() == TOK_COLON)
  {
    // optional block label
    next_token(); // :
    next_token(); // identifier
  }

  while(true)
  {
    if(peek().is_eof())
      return;

    if(peek() == TOK_END)
    {
      next_token(); // end
      break;
    }

    rStatement();
  }

  if(peek() == TOK_COLON)
  {
    // optional block label
    next_token(); // :
    next_token(); // identifier
  }
}

void verilog_indexer_parsert::rFor()
{
  next_token(); // for
  rParenExpression();
  rStatement();
}

void verilog_indexer_parsert::rForEver()
{
  next_token(); // forever
  rStatement();
}

void verilog_indexer_parsert::rWhile()
{
  next_token(); // while
  rParenExpression();
  rStatement();
}

void verilog_indexer_parsert::rIf()
{
  next_token(); // if
  rParenExpression();
  rStatement();
  if(peek() == TOK_ELSE)
  {
    next_token();
    rStatement();
  }
}

void verilog_indexer_parsert::rCase()
{
  next_token(); // case, casex, ...
  rParenExpression();
  while(true)
  {
    if(peek().is_eof())
      return;
    if(peek() == TOK_ENDCASE)
    {
      next_token();
      return;
    }
    rCaseLabel();
    rStatement();
  }
}

void verilog_indexer_parsert::rCaseLabel()
{
  skip_until(':');
}

void verilog_indexer_parsert::rUniquePriority()
{
  // unique case/if ...
  // unique0 case/if ...
  // priority case/if ...
  auto first = next_token(); // unique, unique0, priority
  if(peek() == TOK_IF)
    rIf();
  else if(peek() == TOK_CASE || peek() == TOK_CASEZ || peek() == TOK_CASEX)
    rCase();
  else
  {
    // error
  }
}

void verilog_indexer_parsert::rParenExpression()
{
  auto first = next_token();
  if(first != '(')
    return;
  std::size_t count = 1;

  while(true)
  {
    auto token = next_token();
    if(token.is_eof())
      return;
    else if(token == '(')
      count++;
    else if(token == ')')
    {
      if(count == 1)
        return;
      count--;
    }
  }
}

void verilog_indexer_parsert::rDeclaration()
{
  auto &token = peek();

  if(token == TOK_TYPEDEF)
  {
    next_token(); // typedef
    rType();
  }
  else if(token == TOK_RAND)
  {
    next_token(); // rand
    rType();
  }
  else if(token == TOK_LOCAL)
  {
    next_token(); // local
    rType();
  }
  else if(token == TOK_PARAMETER || token == TOK_LOCALPARAM)
  {
    next_token();
    rTypeOpt();
  }
  else if(
    token == TOK_VAR || token == TOK_INPUT || token == TOK_OUTPUT ||
    token == TOK_INOUT || token == TOK_WIRE || token == TOK_TRI0 ||
    token == TOK_TRI1)
  {
    next_token();
    rTypeOpt();
  }
  else
  {
    rType();
  }

  rDeclarators();
}

void verilog_indexer_parsert::rType()
{
  auto &token = peek();
  if(token == TOK_STRUCT || token == TOK_UNION)
  {
    rStructUnion();
  }
  else if(token == TOK_ENUM)
  {
    rEnum();
  }
  else
  {
    next_token();
  }
}

void verilog_indexer_parsert::rTypeOpt()
{
  auto &token = peek();
  if(
    token == TOK_REG || token == TOK_GENVAR || token == TOK_ENUM ||
    token == TOK_INTEGER || token == TOK_REALTIME || token == TOK_REAL ||
    token == TOK_SHORTREAL || token == TOK_BYTE || token == TOK_SHORTINT ||
    token == TOK_LOGIC || token == TOK_BIT || token == TOK_INT ||
    token == TOK_STRUCT || token == TOK_UNION)
  {
    rType();
  }
}

void verilog_indexer_parsert::rEnum()
{
  next_token();
  skip_until('{');
  skip_until('}');
}

void verilog_indexer_parsert::rStructUnion()
{
  next_token(); // struct or union
  skip_until('{');
  while(!peek().is_eof() && peek() != '}')
  {
    rType();
    rDeclarators();
  }
  skip_until('}');
}

void verilog_indexer_parsert::rDeclarators()
{
  skip_until(';');
}

void verilog_indexer_parsert::rTaskFunction()
{
  if(peek() == TOK_VIRTUAL)
    next_token(); // virtual

  auto first = next_token();  // function or task
  auto second = next_token(); // name

  if(first == TOK_FUNCTION)
    skip_until(TOK_ENDFUNCTION);
  else
    skip_until(TOK_ENDTASK);
}

void verilog_indexer_parsert::rContinuousAssign()
{
  skip_until(';');
}

void verilog_indexer_parsert::rGenerate()
{
  skip_until(TOK_ENDGENERATE);
}

void verilog_indexer_parsert::rGenerateFor()
{
  next_token(); // for
  rParenExpression();
  rItem();
}

void verilog_indexer_parsert::rGenerateIf()
{
  next_token(); // if
  rParenExpression();
  rItem();
  if(peek() == TOK_ELSE)
  {
    next_token();
    rItem();
  }
}

void verilog_indexer_parsert::rGenerateBegin()
{
  next_token(); // begin

  if(peek() == TOK_COLON)
  {
    // optional block label
    next_token(); // :
    next_token(); // identifier
  }

  while(true)
  {
    if(peek().is_eof())
      return;

    if(peek() == TOK_END)
    {
      next_token(); // end
      return;
    }

    rItem();
  }
}

void verilog_indexer_parsert::rProperty()
{
  skip_until(';');
}

void verilog_indexer_parsert::skip_until(int token)
{
  while(true)
  {
    if(peek().is_eof())
      return;
    if(next_token() == token)
      return;
  }
}

void verilog_indexer_parsert::rModuleInstance()
{
  auto first = next_token(); // module or primitive

  if(peek() == '#')
  {
    // Module instance with parameters.
    next_token(); // #
    if(peek() == '(')
    {
      // parameter values
      rParenExpression();
    }
    else
    {
      next_token(); // parameter value
    }
  }

  // the instance name is optional
  if(peek() != '[' && peek() != '(')
  {
    auto second = next_token(); // instance

    idt id;
    id.kind = verilog_indexert::idt::INSTANCE;
    id.name = second.text;
    id.module = current_module;
    id.file_name = verilog_parser.get_file();
    id.line_number = verilog_parser.get_line_no();
    id.instantiated_module = first.text;
    indexer.add(std::move(id));
  }

  if(peek() == '[') // instance range
    skip_until(']');

  if(next_token() != '(') // connections
    return;

  skip_until(';');
}

void verilog_indexer_parsert::rLabeledItem()
{
  // label followed by assert/assume/cover
  next_token();                 // label
  if(next_token() != TOK_COLON) // :
    return;
  skip_until(';');
}

std::vector<std::filesystem::path> verilog_files()
{
  std::vector<std::filesystem::path> result;

  auto current = std::filesystem::current_path();

  for(auto &entry : std::filesystem::recursive_directory_iterator(current))
    if(!is_directory(entry.path()))
      if(has_suffix(entry.path(), ".v") || has_suffix(entry.path(), ".sv"))
        result.push_back(std::filesystem::relative(entry.path()));

  return result;
}

void show_module_hierarchy_rec(
  const irep_idt &module,
  std::size_t indent,
  const verilog_indexert::instancest &instances)
{
  auto m_it = instances.find(module);
  if(m_it != instances.end())
  {
    // We output in the order found in the file,
    // but show the sub-instances of any module only once.
    std::unordered_set<irep_idt> done;
    for(auto &instance : m_it->second)
    {
      std::cout << std::string(indent * 2, ' ') << instance.instantiated_module
                << ' ' << instance.name << '\n';
      if(done.insert(instance.instantiated_module).second)
        show_module_hierarchy_rec(
          instance.instantiated_module, indent + 1, instances);
    }
  }
}

void sort_alphabetically(std::vector<verilog_indexert::idt> &ids)
{
  using idt = verilog_indexert::idt;
  std::sort(ids.begin(), ids.end(), [](const idt &a, const idt &b) {
    return a.name.compare(b.name) < 0;
  });
}

void show_module_hierarchy(const verilog_indexert &indexer)
{
  std::unordered_set<irep_idt, irep_id_hash> instantiated_modules;
  // module -> list of instances
  verilog_indexert::instancest instances;

  for(const auto &[_, file] : indexer.file_map)
    for(const auto &id : file.ids)
      if(id.is_instance())
      {
        instantiated_modules.insert(id.instantiated_module);
        instances[id.module].push_back(id);
      }

  // identify root modules
  std::vector<irep_idt> root_modules;
  for(auto &[_, file] : indexer.file_map)
    for(const auto &id : file.ids)
      if(
        id.is_module() &&
        instantiated_modules.find(id.name) == instantiated_modules.end())
      {
        root_modules.push_back(id.name);
      }

  // sort those alphabetically
  std::sort(
    root_modules.begin(),
    root_modules.end(),
    [](const irep_idt &a, const irep_idt &b) { return a.compare(b) < 0; });

  for(auto &root : root_modules)
  {
    std::cout << root << '\n';
    show_module_hierarchy_rec(root, 1, instances);
  }
}

void show_kind(
  verilog_indexert::idt::kindt kind,
  const verilog_indexert &indexer)
{
  std::vector<verilog_indexert::idt> ids;

  for(const auto &[_, file] : indexer.file_map)
    for(const auto &id : file.ids)
      if(id.kind == kind)
        ids.push_back(id);

  sort_alphabetically(ids);

  for(const auto &id : ids)
  {
    std::cout << id.name << ' ' << id.file_name << " line " << id.line_number
              << '\n';
  }
}

int verilog_index(const cmdlinet &cmdline)
{
  // First find all .v and .sv files
  auto files = verilog_files();

  // Now index them.
  verilog_indexert indexer;

  VerilogStandardt standard = [&cmdline]()
  {
    if(cmdline.isset("1800-2017"))
      return VerilogStandardt::SV2017;
    else if(cmdline.isset("1800-2012"))
      return VerilogStandardt::SV2012;
    else if(cmdline.isset("1800-2009"))
      return VerilogStandardt::SV2009;
    else if(cmdline.isset("1800-2005"))
      return VerilogStandardt::SV2005;
    else if(cmdline.isset("1364-2005"))
      return VerilogStandardt::V2005;
    else if(cmdline.isset("1364-2001"))
      return VerilogStandardt::V2001;
    else if(cmdline.isset("1364-2001-noconfig"))
      return VerilogStandardt::V2001_NOCONFIG;
    else if(cmdline.isset("1364-1995"))
      return VerilogStandardt::V1995;
    else // default
      return VerilogStandardt::SV2017;
  }();

  for(const auto &file : files)
  {
    indexer(std::string(file), standard);
  }

  if(cmdline.isset("module-hierarchy"))
  {
    show_module_hierarchy(indexer);
  }
  else if(cmdline.isset("modules"))
  {
    // Show the modules.
    show_kind(verilog_indexert::idt::kindt::MODULE, indexer);
  }
  else if(cmdline.isset("packages"))
  {
    // Show the packages.
    show_kind(verilog_indexert::idt::kindt::PACKAGE, indexer);
  }
  else if(cmdline.isset("interfaces"))
  {
    // Show the interfaces.
    show_kind(verilog_indexert::idt::kindt::INTERFACE, indexer);
  }
  else if(cmdline.isset("classes"))
  {
    // Show the interfaces.
    show_kind(verilog_indexert::idt::kindt::CLASS, indexer);
  }
  else if(cmdline.isset("udps"))
  {
    // Show the interfaces.
    show_kind(verilog_indexert::idt::kindt::UDP, indexer);
  }
  else
  {
    auto total_number_of = indexer.total_number_of();
    using idt = verilog_indexert::idt;
    std::cout << "Number of files....: " << indexer.total_number_of_files()
              << '\n';
    std::cout << "Number of lines....: " << indexer.total_number_of_lines()
              << '\n';
    std::cout << "Number of modules..: " << total_number_of[idt::MODULE]
              << '\n';
    std::cout << "Number of UDPs.....: " << total_number_of[idt::UDP] << '\n';
    std::cout << "Number of classes..: " << total_number_of[idt::CLASS] << '\n';
    std::cout << "Number of packages.: " << total_number_of[idt::PACKAGE]
              << '\n';
    std::cout << "Number of functions: " << total_number_of[idt::FUNCTION]
              << '\n';
    std::cout << "Number of tasks....: " << total_number_of[idt::TASK] << '\n';
  }

  return 0;
}
