/*******************************************************************\

Module: VHDL Type Checker

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "vhdl_typecheck.h"
#include "vhdl_typecheck_class.h"

#include <util/arith_tools.h>
#include <util/symbol_table_base.h>

#include <algorithm>

irep_idt vhdl_typecheckt::to_lower(const irep_idt &src)
{
  std::string data = id2string(src);
  std::transform(data.begin(), data.end(), data.begin(), ::tolower);
  return data;
}

const typet *vhdl_typecheckt::lookup_type(const irep_idt &name) const
{
  for(auto s_it = scopes.rbegin(); s_it != scopes.rend(); s_it++)
  {
    auto t_it = s_it->types.find(name);
    if(t_it != s_it->types.end())
      return &t_it->second;
  }

  return nullptr;
}

const exprt *vhdl_typecheckt::lookup_constant(const irep_idt &name) const
{
  for(auto s_it = scopes.rbegin(); s_it != scopes.rend(); s_it++)
  {
    auto c_it = s_it->constants.find(name);
    if(c_it != s_it->constants.end())
      return &c_it->second;
  }

  return nullptr;
}

const exprt *
vhdl_typecheckt::lookup_enumeration_literal(const irep_idt &name) const
{
  for(auto s_it = scopes.rbegin(); s_it != scopes.rend(); s_it++)
  {
    auto e_it = s_it->enumeration_literals.find(name);
    if(e_it != s_it->enumeration_literals.end())
      return &e_it->second;
  }

  return nullptr;
}

const symbolt *vhdl_typecheckt::lookup_object(const irep_idt &name) const
{
  for(auto s_it = scopes.rbegin(); s_it != scopes.rend(); s_it++)
  {
    auto i_it = s_it->identifiers.find(name);
    if(i_it != s_it->identifiers.end())
      return &symbol_table.lookup_ref(i_it->second);
  }

  return nullptr;
}

symbolt &vhdl_typecheckt::add_object(
  const irep_idt &base_name,
  const typet &type,
  const source_locationt &source_location)
{
  auto lower_case_name = to_lower(base_name);

  if(current_scope().identifiers.count(lower_case_name) != 0)
    throw errort{}.with_location(source_location)
      << "object `" << base_name << "' is already declared";

  symbolt new_symbol;
  new_symbol.base_name = base_name;
  new_symbol.name =
    id2string(current_scope().prefix) + "." + id2string(lower_case_name);
  new_symbol.pretty_name = new_symbol.name;
  new_symbol.type = type;
  new_symbol.mode = ID_VHDL;
  new_symbol.module = module_symbol->name;
  new_symbol.location = source_location;

  current_scope().identifiers[lower_case_name] = new_symbol.name;

  symbolt *symbol_ptr;
  if(symbol_table.move(new_symbol, symbol_ptr))
    throw errort{}.with_location(source_location)
      << "symbol `" << new_symbol.name << "' is already in the symbol table";

  return *symbol_ptr;
}

/// The predefined types of package STANDARD (IEEE 1076-2008 16.3), and
/// the types of the IEEE packages std_logic_1164 and numeric_std.
/// Use clauses are not checked.
void vhdl_typecheckt::add_predefined_types()
{
  auto &scope = current_scope();

  scope.types["boolean"] = vhdl_boolean_type();
  scope.types["bit"] = vhdl_bit_type();
  scope.types["integer"] = vhdl_integer_type();
  scope.types["natural"] = vhdl_natural_type();
  scope.types["positive"] = vhdl_positive_type();
  scope.types["std_logic"] = vhdl_std_logic_type();
  scope.types["std_ulogic"] = vhdl_std_ulogic_type();

  // the unconstrained array types; the range is set when constrained
  for(auto name :
      {"bit_vector", "std_logic_vector", "std_ulogic_vector", "unsigned"})
  {
    typet type(ID_unconstrained);
    type.set(ID_C_vhdl_type, name);
    scope.types[name] = type;
  }

  {
    typet type(ID_unconstrained);
    type.set(ID_C_vhdl_type, "signed");
    scope.types["signed"] = type;
  }

  // the literals of boolean
  exprt false_literal = false_exprt{}, true_literal = true_exprt{};
  false_literal.type() = vhdl_boolean_type();
  true_literal.type() = vhdl_boolean_type();
  scope.enumeration_literals["false"] = false_literal;
  scope.enumeration_literals["true"] = true_literal;
}

void vhdl_typecheckt::typecheck_interface_list(
  const irept &list,
  bool is_generic)
{
  for(auto &element : list.get_sub())
  {
    auto type = typecheck_subtype_indication(
      static_cast<const typet &>(element.find(ID_type)));

    auto &init = static_cast<const exprt &>(element.find(ID_init));

    exprt init_value = nil_exprt{};
    if(init.is_not_nil())
      init_value = convert_to(typecheck_expr(init), type);

    irep_idt mode = element.get(ID_mode);

    for(auto &declarator : element.find(ID_declarators).get_sub())
    {
      irep_idt base_name = declarator.get(ID_identifier);
      auto &source_location =
        static_cast<const exprt &>(declarator).source_location();

      if(is_generic)
      {
        // Generics are constants. We only support the default value.
        if(init_value.is_nil())
          throw errort{}.with_location(source_location)
            << "generic `" << base_name << "' has no default value";

        current_scope().constants[to_lower(base_name)] = init_value;
      }
      else
      {
        auto &symbol = add_object(base_name, type, source_location);
        // inout ports may be driven by the environment, and are
        // treated like inputs unless assigned
        symbol.is_input = mode == ID_input || mode == ID_inout;
        symbol.is_output =
          mode == ID_output || mode == ID_buffer || mode == ID_inout;
        symbol.is_state_var = !symbol.is_input;
        symbol.value = init_value;
      }
    }
  }
}

void vhdl_typecheckt::typecheck_entity(const vhdl_parse_treet::itemt &entity)
{
  typecheck_interface_list(entity.find(ID_generics), true);
  typecheck_interface_list(entity.find(ID_ports), false);
  typecheck_declarations(entity.find(ID_decl));
}

void vhdl_typecheckt::typecheck_declarations(const irept &decl)
{
  for(auto &d : decl.get_sub())
    typecheck_declaration(d);
}

void vhdl_typecheckt::typecheck_declaration(const irept &decl)
{
  auto &source_location = static_cast<const exprt &>(decl).source_location();

  if(
    decl.id() == ID_signal || decl.id() == ID_variable ||
    decl.id() == ID_constant)
  {
    typecheck_object_declaration(decl);
  }
  else if(decl.id() == ID_vhdl_type_decl)
  {
    typecheck_type_declaration(decl);
  }
  else if(decl.id() == ID_vhdl_subtype_decl)
  {
    typecheck_subtype_declaration(decl);
  }
  else if(decl.id() == ID_component)
  {
    throw errort{}.with_location(source_location)
      << "component declarations are not supported yet";
  }
  else if(decl.id() == ID_vhdl_function || decl.id() == ID_vhdl_procedure)
  {
    throw errort{}.with_location(source_location)
      << "subprogram declarations are not supported yet";
  }
  else if(decl.id() == ID_alias)
  {
    throw errort{}.with_location(source_location)
      << "alias declarations are not supported yet";
  }
  else
    throw errort{}.with_location(source_location)
      << "unexpected declaration: " << decl.id();
}

void vhdl_typecheckt::typecheck_object_declaration(const irept &decl)
{
  auto type = typecheck_subtype_indication(
    static_cast<const typet &>(decl.find(ID_type)));

  auto &init = static_cast<const exprt &>(decl.find(ID_init));

  exprt init_value;

  if(init.is_not_nil())
    init_value = convert_to(typecheck_expr(init), type);
  else if(decl.id() == ID_constant)
    throw errort{}.with_location(
      static_cast<const exprt &>(decl).source_location())
      << "constant declaration requires a value";
  else
    init_value = default_value(type);

  for(auto &declarator : decl.find(ID_declarators).get_sub())
  {
    irep_idt base_name = declarator.get(ID_identifier);
    auto &source_location =
      static_cast<const exprt &>(declarator).source_location();

    if(decl.id() == ID_constant)
    {
      auto lower_case_name = to_lower(base_name);
      if(current_scope().constants.count(lower_case_name) != 0)
        throw errort{}.with_location(source_location)
          << "constant `" << base_name << "' is already declared";
      current_scope().constants[lower_case_name] = init_value;
    }
    else
    {
      auto &symbol = add_object(base_name, type, source_location);
      symbol.is_state_var = true;
      symbol.value = init_value;
      // Variables are marked as auxiliary; this distinguishes them
      // from signals, which matters for assignments within a process.
      symbol.is_auxiliary = decl.id() == ID_variable;
    }
  }
}

void vhdl_typecheckt::typecheck_type_declaration(const irept &decl)
{
  auto &name = static_cast<const exprt &>(decl.find(ID_name));
  irep_idt base_name = name.get(ID_identifier);
  auto lower_case_name = to_lower(base_name);

  auto type = typecheck_type(static_cast<const typet &>(decl.find(ID_type)));
  type.set(ID_C_vhdl_type, lower_case_name);

  if(type.id() == ID_enumeration)
  {
    // the literals become constants of the type
    for(auto &element : to_enumeration_type(type).elements())
    {
      irep_idt literal = element.id();
      if(current_scope().enumeration_literals.count(literal) != 0)
        throw errort{}.with_location(name.source_location())
          << "enumeration literal `" << literal << "' is already declared";
      current_scope().enumeration_literals[literal] =
        constant_exprt{literal, type};
    }
  }

  if(current_scope().types.count(lower_case_name) != 0)
    throw errort{}.with_location(name.source_location())
      << "type `" << base_name << "' is already declared";

  current_scope().types[lower_case_name] = type;
}

void vhdl_typecheckt::typecheck_subtype_declaration(const irept &decl)
{
  auto &name = static_cast<const exprt &>(decl.find(ID_name));
  irep_idt base_name = name.get(ID_identifier);
  auto lower_case_name = to_lower(base_name);

  auto type = typecheck_subtype_indication(
    static_cast<const typet &>(decl.find(ID_type)));

  if(current_scope().types.count(lower_case_name) != 0)
    throw errort{}.with_location(name.source_location())
      << "type `" << base_name << "' is already declared";

  current_scope().types[lower_case_name] = type;
}

exprt vhdl_typecheckt::typecheck_concurrent_statements(const irept &body)
{
  exprt result(ID_body);

  for(auto &statement : body.get_sub())
    result.add_to_operands(
      typecheck_concurrent_statement(static_cast<const exprt &>(statement)));

  return result;
}

exprt vhdl_typecheckt::typecheck_concurrent_statement(exprt statement)
{
  if(statement.id() == ID_process)
  {
    return typecheck_process(std::move(statement));
  }
  else if(statement.id() == ID_assert)
  {
    return typecheck_assert(std::move(statement));
  }
  else if(statement.id() == ID_vhdl_signal_assign)
  {
    return typecheck_assign(std::move(statement), true);
  }
  else if(statement.id() == ID_vhdl_selected_assign)
  {
    return typecheck_selected_assign(std::move(statement), true);
  }
  else if(statement.id() == ID_inst)
  {
    throw errort{}.with_location(statement.source_location())
      << "component instantiation is not supported yet";
  }
  else if(statement.id() == ID_generate_if || statement.id() == ID_generate_for)
  {
    throw errort{}.with_location(statement.source_location())
      << "generate statements are not supported yet";
  }
  else
    throw errort{}.with_location(statement.source_location())
      << "unexpected concurrent statement: " << statement.id();
}

exprt vhdl_typecheckt::typecheck_process(exprt process)
{
  // the declarations are scoped to the process
  irep_idt label = process.get(ID_label);
  irep_idt prefix =
    id2string(current_scope().prefix) + "." +
    (label.empty() ? "process" + std::to_string(++process_counter)
                   : id2string(to_lower(label)));

  push_scope(prefix);

  typecheck_declarations(process.find(ID_decl));

  // the sensitivity list; we check that the names resolve
  auto &sensitivity = static_cast<const exprt &>(process.find(ID_sensitivity));

  if(sensitivity.id() != ID_all && sensitivity.is_not_nil())
  {
    exprt new_sensitivity{ID_sensitivity};
    for(auto &name : sensitivity.get_sub())
    {
      auto typed = typecheck_expr(static_cast<const exprt &>(name));
      new_sensitivity.move_to_sub(typed);
    }
    process.add(ID_sensitivity) = std::move(new_sensitivity);
  }

  // the body
  auto &body = to_unary_expr(process).op();
  body = typecheck_code(std::move(body));

  pop_scope();

  return process;
}

codet vhdl_typecheckt::typecheck_code(exprt statement)
{
  if(statement.id() == ID_block)
    return typecheck_block(std::move(statement));
  else if(statement.id() == ID_assert)
    return typecheck_assert(std::move(statement));
  else if(statement.id() == ID_assign)
    return typecheck_assign(std::move(statement), false);
  else if(statement.id() == ID_vhdl_signal_assign)
    return typecheck_assign(std::move(statement), true);
  else if(statement.id() == ID_vhdl_selected_assign)
    return typecheck_selected_assign(
      std::move(statement), !statement.get_bool(ID_variable));
  else if(statement.id() == ID_ifthenelse)
    return typecheck_if(std::move(statement));
  else if(statement.id() == ID_switch_case)
    return typecheck_case(std::move(statement));
  else if(statement.id() == ID_for)
    return typecheck_for(std::move(statement));
  else if(statement.id() == ID_skip)
    return code_skipt{};
  else if(statement.id() == ID_report)
    return code_skipt{}; // no effect on the hardware
  else if(statement.id() == ID_wait)
    throw errort{}.with_location(statement.source_location())
      << "wait statements are not supported";
  else if(
    statement.id() == ID_while || statement.id() == ID_vhdl_exit ||
    statement.id() == ID_vhdl_next)
    throw errort{}.with_location(statement.source_location())
      << statement.id() << " statements are not supported yet";
  else if(statement.id() == ID_function_call)
    throw errort{}.with_location(statement.source_location())
      << "procedure calls are not supported yet";
  else if(statement.id() == ID_return)
    throw errort{}.with_location(statement.source_location())
      << "return statement outside of a subprogram";
  else
    throw errort{}.with_location(statement.source_location())
      << "unexpected statement: " << statement.id();
}

codet vhdl_typecheckt::typecheck_block(exprt block)
{
  code_blockt result;
  result.add_source_location() = block.source_location();

  for(auto &statement : block.operands())
    result.add(typecheck_code(std::move(statement)));

  return std::move(result);
}

codet vhdl_typecheckt::typecheck_assert(exprt assertion)
{
  auto &condition = to_unary_expr(assertion).op();
  condition = convert_to(typecheck_expr(condition), vhdl_boolean_type());

  // the report must be a string
  auto &report = static_cast<const exprt &>(assertion.find(ID_report));

  if(
    report.is_not_nil() &&
    (report.id() != ID_constant || report.type().id() != ID_string))
    throw errort{}.with_location(report.source_location())
      << "report clause expects a string literal";

  // the severity level is ignored

  codet result(ID_assert);
  result.add_source_location() = assertion.source_location();
  result.add_to_operands(std::move(condition));
  result.add(ID_report) = report;
  if(!assertion.get(ID_label).empty())
    result.set(ID_label, assertion.get(ID_label));

  return result;
}

codet vhdl_typecheckt::typecheck_assign(exprt assignment, bool is_signal)
{
  auto &binary = to_binary_expr(assignment);
  auto target = typecheck_expr(binary.lhs());

  // the target must be an object of the right class
  // the object is operand 0 of these
  exprt base = target;
  while(base.id() == ID_extractbit || base.id() == ID_extractbits ||
        base.id() == ID_index || base.id() == ID_member)
    base = base.operands().front();

  if(base.id() != ID_symbol)
    throw errort{}.with_location(target.source_location())
      << "assignment target must be a signal or a variable";

  auto &symbol = ns.lookup(to_symbol_expr(base));
  bool target_is_signal = !symbol.is_auxiliary;

  if(symbol.is_input && !symbol.is_output)
    throw errort{}.with_location(target.source_location())
      << "cannot assign to input port `" << symbol.base_name << "'";

  if(is_signal && !target_is_signal)
    throw errort{}.with_location(target.source_location())
      << "signal assignment to variable `" << symbol.base_name << "'";

  if(!is_signal && target_is_signal)
    throw errort{}.with_location(target.source_location())
      << "variable assignment to signal `" << symbol.base_name << "'";

  // The value is an expression, or a conditional, i.e., nested if
  // expressions with a nil else branch when there is none, for the
  // VHDL-2008 conditional assignment.
  auto convert_value = [this, &target](exprt value) -> exprt
  {
    std::function<exprt(exprt)> rec = [&](exprt v) -> exprt
    {
      if(v.id() == ID_if)
      {
        auto &if_expr = to_if_expr(v);
        if_expr.cond() =
          convert_to(typecheck_expr(if_expr.cond()), vhdl_boolean_type());
        if_expr.true_case() = rec(if_expr.true_case());
        if(if_expr.false_case().is_not_nil())
          if_expr.false_case() = rec(if_expr.false_case());
        if_expr.type() = target.type();
        return std::move(if_expr);
      }
      else if(v.is_nil())
        return v; // unaffected
      else
        return convert_to(typecheck_expr(v), target.type());
    };
    return rec(std::move(value));
  };

  auto value = convert_value(binary.rhs());

  codet result(is_signal ? ID_vhdl_signal_assign : ID_assign);
  result.add_source_location() = assignment.source_location();
  result.add_to_operands(std::move(target), std::move(value));
  if(!assignment.get(ID_label).empty())
    result.set(ID_label, assignment.get(ID_label));

  return result;
}

/// The condition for the given choices, as used in case statements,
/// selected assignments and aggregates.
exprt vhdl_typecheckt::case_condition(
  const exprt &selector,
  const irept &choices)
{
  exprt::operandst disjuncts;

  for(auto &choice_irep : choices.get_sub())
  {
    auto &choice = static_cast<const exprt &>(choice_irep);

    if(choice.id() == ID_others)
      return true_exprt{};
    else if(choice.id() == ID_range)
    {
      auto range = typecheck_range(choice);
      auto low = convert_to(
        from_integer(range.low(), vhdl_integer_type()), selector.type());
      auto high = convert_to(
        from_integer(range.high(), vhdl_integer_type()), selector.type());
      disjuncts.push_back(and_exprt{
        binary_relation_exprt{low, ID_le, selector},
        binary_relation_exprt{selector, ID_le, high}});
    }
    else
    {
      auto value = convert_to(typecheck_expr(choice), selector.type());
      disjuncts.push_back(equal_exprt{selector, value});
    }
  }

  return disjunction(disjuncts);
}

codet vhdl_typecheckt::typecheck_selected_assign(
  exprt assignment,
  bool is_signal)
{
  // We turn this into nested if statements.
  auto &binary = to_binary_expr(assignment);
  auto selector = typecheck_expr(binary.rhs());

  auto &case_items = assignment.find(ID_choices).get_sub();

  // we go backwards, starting from the last alternative
  codet result = code_skipt{};
  bool first = true;

  for(auto it = case_items.rbegin(); it != case_items.rend(); it++)
  {
    auto &case_item = static_cast<const exprt &>(*it);
    exprt plain_assignment(is_signal ? ID_vhdl_signal_assign : ID_assign);
    plain_assignment.add_source_location() = case_item.source_location();
    plain_assignment.add_to_operands(
      exprt{binary.lhs()}, exprt{to_unary_expr(case_item).op()});
    auto assignment_code = typecheck_assign(plain_assignment, is_signal);

    auto condition = case_condition(selector, case_item.find(ID_choices));

    if(first && condition.is_true())
      result = std::move(assignment_code); // others
    else
      result = code_ifthenelset{
        std::move(condition), std::move(assignment_code), std::move(result)};

    first = false;
  }

  result.add_source_location() = assignment.source_location();
  return result;
}

codet vhdl_typecheckt::typecheck_if(exprt if_statement)
{
  auto &operands = if_statement.operands();
  DATA_INVARIANT(operands.size() == 3, "if statements have three operands");

  auto condition = convert_to(typecheck_expr(operands[0]), vhdl_boolean_type());
  auto then_case = typecheck_code(operands[1]);

  code_ifthenelset result{std::move(condition), std::move(then_case)};
  result.add_source_location() = if_statement.source_location();

  if(operands[2].is_not_nil())
    result.else_case() = typecheck_code(operands[2]);

  return std::move(result);
}

codet vhdl_typecheckt::typecheck_case(exprt case_statement)
{
  // We turn this into nested if statements.
  auto selector = typecheck_expr(to_unary_expr(case_statement).op());

  auto &case_items = case_statement.find(ID_case_item).get_sub();

  // we go backwards, starting from the last alternative
  codet result = code_skipt{};
  bool first = true;

  for(auto it = case_items.rbegin(); it != case_items.rend(); it++)
  {
    auto &case_item = static_cast<const exprt &>(*it);
    auto body = typecheck_code(to_unary_expr(case_item).op());
    auto condition = case_condition(selector, case_item.find(ID_choices));

    if(first && condition.is_true())
      result = std::move(body); // others
    else
    {
      code_ifthenelset if_code{
        std::move(condition), std::move(body), std::move(result)};
      if_code.add_source_location() = case_item.source_location();
      result = std::move(if_code);
    }

    first = false;
  }

  result.add_source_location() = case_statement.source_location();
  return result;
}

codet vhdl_typecheckt::typecheck_for(exprt for_statement)
{
  // The loop is unrolled; the loop variable is a constant in each copy.
  auto &range_expr = static_cast<const exprt &>(for_statement.find(ID_range));
  auto range = typecheck_range(range_expr);

  auto &loop_variable =
    static_cast<const exprt &>(for_statement.find(ID_loop_variable));
  auto lower_case_name = to_lower(loop_variable.get(ID_identifier));

  if(range.length() > 10000)
    throw errort{}.with_location(for_statement.source_location())
      << "loop range too large";

  code_blockt result;
  result.add_source_location() = for_statement.source_location();

  // IEEE 1076-2008 10.10: a null range executes zero times
  if(range.length() == 0)
    return std::move(result);

  auto &body = to_unary_expr(for_statement).op();

  for(mp_integer i = range.left;;
      i += (range.direction == vhdl_directiont::TO ? 1 : -1))
  {
    push_scope(current_scope().prefix);
    current_scope().constants[lower_case_name] =
      from_integer(i, vhdl_integer_type());
    result.add(typecheck_code(body));
    pop_scope();

    if(i == range.right)
      break;
  }

  return std::move(result);
}

void vhdl_typecheckt::typecheck_architecture(
  const vhdl_parse_treet::itemt &item)
{
  // create the module symbol

  symbolt symbol;

  symbol.mode = ID_VHDL;
  symbol.name = "vhdl::" + id2string(module_name);
  symbol.type = typet(ID_module);
  symbol.base_name = module_name;
  symbol.pretty_name = module_name;
  symbol.module = symbol.name;
  symbol.location = item.source_location();

  symbolt *new_symbol;

  if(symbol_table.move(symbol, new_symbol))
    throw errort{}.with_location(item.source_location())
      << "duplicate definition of module " << symbol.base_name;

  module_symbol = new_symbol;

  push_scope(module_symbol->name);
  add_predefined_types();

  // the packages in the design file
  for(auto &p : parse_tree.items)
    if(p.is_package())
      typecheck_package(p);

  // find the entity
  const vhdl_parse_treet::itemt *entity = nullptr;
  for(auto &e : parse_tree.items)
    if(e.is_entity() && e.get_identifier() == module_name)
      entity = &e;

  if(entity == nullptr)
    throw errort{}.with_location(item.source_location())
      << "entity `" << module_name << "' not found";

  typecheck_entity(*entity);

  // now the architecture
  typecheck_declarations(item.find(ID_decl));

  auto body = typecheck_concurrent_statements(item.find(ID_body));

  pop_scope();

  new_symbol->value.id(ID_module);
  new_symbol->value.add(ID_body) = std::move(body);
}

/// We only support constants, types and subtypes in packages.
void vhdl_typecheckt::typecheck_package(const vhdl_parse_treet::itemt &package)
{
  for(auto &d : package.find(ID_decl).get_sub())
  {
    if(
      d.id() == ID_constant || d.id() == ID_vhdl_type_decl ||
      d.id() == ID_vhdl_subtype_decl)
    {
      typecheck_declaration(d);
    }
    else if(d.id() == ID_vhdl_function || d.id() == ID_vhdl_procedure)
    {
      // ignored, as they cannot be called yet
    }
    else
      throw errort{}.with_location(
        static_cast<const exprt &>(d).source_location())
        << "unsupported package declaration: " << d.id();
  }
}

void vhdl_typecheckt::typecheck()
{
  // The module is an entity. Find the architecture for the entity
  // in the parse tree; when there are multiple, the last one is used.
  const vhdl_parse_treet::itemt *architecture = nullptr;

  for(const auto &item : parse_tree.items)
    if(item.is_architecture() && module_name == item.get_entity_identifier())
      architecture = &item;

  if(architecture == nullptr)
    throw errort{} << "no architecture found for entity `" << module_name
                   << "'";

  typecheck_architecture(*architecture);
}

bool vhdl_typecheck(
  const vhdl_parse_treet &parse_tree,
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler)
{
  return vhdl_typecheckt(parse_tree, module, symbol_table, message_handler)
    .typecheck_main();
}
