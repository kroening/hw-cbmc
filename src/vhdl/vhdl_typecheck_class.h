/*******************************************************************\

Module: VHDL Type Checking

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_VHDL_VHDL_TYPECHECK_CLASS_H
#define CPROVER_VHDL_VHDL_TYPECHECK_CLASS_H

#include <util/namespace.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table_base.h>
#include <util/typecheck.h>

#include "vhdl_parse_tree.h"
#include "vhdl_types.h"

#include <map>
#include <vector>

/// Type checking of the architecture for one entity.
///
/// The result is a module symbol whose value has
/// * `ID_body`, the concurrent statements, type checked;
///
/// and symbols for the ports, signals and process variables of the
/// design. Constants are inlined, and enumeration literals are
/// turned into constants of the enumeration type.
///
/// Statements are turned into codet. Names are resolved into
/// symbol expressions, indexed names into extractbit/extractbits/index
/// expressions, and the clock edge expressions clk'event and
/// rising_edge(clk) into vhdl_event and vhdl_rising_edge.
class vhdl_typecheckt : public typecheckt
{
public:
  vhdl_typecheckt(
    const vhdl_parse_treet &_parse_tree,
    const irep_idt &_module_name,
    symbol_table_baset &_symbol_table,
    message_handlert &_message_handler)
    : typecheckt(_message_handler),
      ns(_symbol_table),
      parse_tree(_parse_tree),
      module_name(_module_name),
      symbol_table(_symbol_table)
  {
  }

  void typecheck() override;

protected:
  const namespacet ns;
  const vhdl_parse_treet &parse_tree;
  const irep_idt &module_name;
  symbolt *module_symbol = nullptr;
  symbol_table_baset &symbol_table;

  // design units
  void typecheck_architecture(const vhdl_parse_treet::itemt &);
  void typecheck_entity(const vhdl_parse_treet::itemt &);
  void typecheck_package(const vhdl_parse_treet::itemt &);

  // declarations
  void typecheck_interface_list(const irept &, bool is_generic);
  void typecheck_declarations(const irept &decl);
  void typecheck_declaration(const irept &decl);
  void typecheck_object_declaration(const irept &decl);
  void typecheck_type_declaration(const irept &decl);
  void typecheck_subtype_declaration(const irept &decl);

  // concurrent statements
  exprt typecheck_concurrent_statements(const irept &);
  exprt typecheck_concurrent_statement(exprt);
  exprt typecheck_process(exprt);

  // sequential statements
  codet typecheck_code(exprt);
  codet typecheck_block(exprt);
  codet typecheck_assert(exprt);
  codet typecheck_assign(exprt, bool is_signal);
  codet typecheck_selected_assign(exprt, bool is_signal);
  codet typecheck_if(exprt);
  codet typecheck_case(exprt);
  codet typecheck_for(exprt);

  /// turn a case statement into nested if statements
  exprt case_condition(const exprt &selector, const irept &choices);

  // types
  typet typecheck_type(const typet &);
  typet typecheck_subtype_indication(const typet &);
  typet typecheck_type_mark(const exprt &name);
  vhdl_ranget typecheck_range(const exprt &);
  mp_integer typecheck_static_integer(const exprt &);

  /// the default initial value of a type (T'left), or nil
  exprt default_value(const typet &);

  // expressions
  exprt typecheck_expr(exprt);
  exprt typecheck_expr_rec(exprt);
  exprt typecheck_symbol(symbol_exprt);
  exprt typecheck_indexed(exprt);
  exprt typecheck_slice(exprt);
  exprt typecheck_attribute(exprt);
  exprt typecheck_function_call(
    const irep_idt &name,
    exprt::operandst arguments,
    const source_locationt &);
  exprt typecheck_type_conversion(exprt, const typet &);
  exprt typecheck_binary(exprt);
  exprt typecheck_unary(exprt);
  exprt typecheck_aggregate(exprt, const typet &);

  /// implicit conversion of universal literals, and type matching
  exprt convert_to(exprt, const typet &);
  /// make the types of two operands match
  void match_types(exprt &, exprt &);

  /// the bit position of a VHDL index into a vector
  exprt vector_bit_index(const exprt &index, const typet &vector_type);

  // scopes
  struct scopet
  {
    irep_idt prefix;                          // for new symbols
    std::map<irep_idt, irep_idt> identifiers; // lower-case base name -> symbol
    std::map<irep_idt, typet> types;
    std::map<irep_idt, exprt> constants;
    std::map<irep_idt, exprt> enumeration_literals;
  };

  std::vector<scopet> scopes;

  void push_scope(const irep_idt &prefix)
  {
    scopes.push_back(scopet{prefix, {}, {}, {}, {}});
  }

  void pop_scope()
  {
    scopes.pop_back();
  }

  scopet &current_scope()
  {
    return scopes.back();
  }

  /// lower-case identifier
  static irep_idt to_lower(const irep_idt &);

  const typet *lookup_type(const irep_idt &lower_case_name) const;
  const exprt *lookup_constant(const irep_idt &lower_case_name) const;
  const exprt *
  lookup_enumeration_literal(const irep_idt &lower_case_name) const;
  const symbolt *lookup_object(const irep_idt &lower_case_name) const;

  /// add a symbol for an object, in the current scope
  symbolt &add_object(
    const irep_idt &base_name,
    const typet &,
    const source_locationt &);

  void add_predefined_types();

  unsigned process_counter = 0;
};

#endif
