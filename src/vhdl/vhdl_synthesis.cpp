/*******************************************************************\

Module: VHDL Synthesis

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "vhdl_synthesis.h"
#include "vhdl_synthesis_class.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/expr_util.h>
#include <util/mathematical_expr.h>
#include <util/simplify_expr.h>

#include <temporal-logic/ltl.h>
#include <trans-word-level/next_symbol.h>

#include "vhdl_types.h"

const symbolt &vhdl_synthesist::lookup(const symbol_exprt &symbol_expr) const
{
  return symbol_table.lookup_ref(symbol_expr.get_identifier());
}

bool vhdl_synthesist::contains_clock_edge(const exprt &expr)
{
  if(
    expr.id() == ID_vhdl_event || expr.id() == ID_vhdl_rising_edge ||
    expr.id() == ID_vhdl_falling_edge)
    return true;

  for(auto &op : expr.operands())
    if(contains_clock_edge(op))
      return true;

  return false;
}

/// IEEE 1076.6-2004 6.1.3.1: the clock edge conditions are
/// clk'event and clk = '1', clk'event and clk = '0', rising_edge(clk)
/// and falling_edge(clk).
bool vhdl_synthesist::is_clock_edge(const exprt &expr)
{
  std::optional<clockt> this_clock;

  if(expr.id() == ID_vhdl_rising_edge || expr.id() == ID_vhdl_falling_edge)
  {
    auto &signal = to_unary_expr(expr).op();
    if(signal.id() != ID_symbol)
      return false;
    this_clock = clockt{
      to_symbol_expr(signal).get_identifier(),
      expr.id() == ID_vhdl_rising_edge};
  }
  else if(expr.id() == ID_and && expr.operands().size() == 2)
  {
    auto &op0 = to_binary_expr(expr).op0();
    auto &op1 = to_binary_expr(expr).op1();

    // the level the clock is compared with, if this is a level check
    auto level_of =
      [](const exprt &event, const exprt &level) -> std::optional<bool>
    {
      if(event.id() != ID_vhdl_event)
        return {};
      auto &clock = to_unary_expr(event).op();
      if(clock.id() != ID_symbol)
        return {};
      if(level == clock)
        return true;
      if(level.id() == ID_not && to_not_expr(level).op() == clock)
        return false;
      if(level.id() == ID_equal || level.id() == ID_notequal)
      {
        auto &lhs = to_binary_expr(level).lhs();
        auto &rhs = to_binary_expr(level).rhs();
        const exprt *other = lhs == clock   ? &rhs
                             : rhs == clock ? &lhs
                                            : nullptr;
        if(other == nullptr || !other->is_constant())
          return {};
        bool value = other->is_true();
        return level.id() == ID_equal ? value : !value;
      }
      return {};
    };

    auto level = level_of(op0, op1);
    const exprt *event = &op0;
    if(!level.has_value())
    {
      level = level_of(op1, op0);
      event = &op1;
    }

    if(!level.has_value())
      return false;

    this_clock = clockt{
      to_symbol_expr(to_unary_expr(*event).op()).get_identifier(), *level};
  }
  else
    return false;

  // there is one clock
  if(clock.has_value())
  {
    if(
      clock->signal != this_clock->signal ||
      clock->rising != this_clock->rising)
    {
      error().source_location = expr.source_location();
      error() << "multiple clocks or clock edges are not supported" << eom;
      throw 0;
    }
  }
  else
    clock = this_clock;

  return true;
}

void vhdl_synthesist::check_no_clock_edge(const exprt &expr) const
{
  if(contains_clock_edge(expr))
  {
    error().source_location = expr.source_location();
    error() << "clock edge outside of a clock edge condition" << eom;
    throw 0;
  }
}

exprt vhdl_synthesist::value_of(
  const symbol_exprt &symbol_expr,
  const valuest &values) const
{
  auto v_it = values.find(symbol_expr.get_identifier());
  if(v_it == values.end())
    return symbol_expr;
  else
    return v_it->second;
}

/// Variables see the values assigned earlier in the process
/// (IEEE 1076-2008 10.6.2.1); signals see the value from the
/// beginning of the process (10.5.2.2).
exprt vhdl_synthesist::substitute(exprt expr, const valuest &values) const
{
  if(expr.id() == ID_symbol)
  {
    auto &symbol_expr = to_symbol_expr(expr);
    auto &symbol = lookup(symbol_expr);
    if(symbol.is_auxiliary) // a variable
      return value_of(symbol_expr, values);
    else
      return expr;
  }
  else
  {
    for(auto &op : expr.operands())
      op = substitute(std::move(op), values);
    return expr;
  }
}

exprt vhdl_synthesist::current_value(exprt target, const valuest &values) const
{
  if(target.id() == ID_symbol)
    return value_of(to_symbol_expr(target), values);
  else
  {
    // the object is operand 0
    auto &object = target.operands().front();
    object = current_value(object, values);
    return target;
  }
}

exprt vhdl_synthesist::substitute_indices(exprt target, const valuest &values)
  const
{
  if(target.id() == ID_symbol)
    return target;
  else
  {
    auto &operands = target.operands();
    operands.front() = substitute_indices(operands.front(), values);
    for(std::size_t i = 1; i < operands.size(); i++)
      operands[i] = substitute(operands[i], values);
    return target;
  }
}

/// IEEE 1076-2008 10.5.2.1: the target may be a name, an indexed name,
/// a slice, or a selected name. We compute the new value of the
/// entire object.
exprt vhdl_synthesist::assign_to(
  const exprt &lhs,
  exprt rhs,
  const valuest &values) const
{
  if(lhs.id() == ID_symbol)
  {
    return rhs;
  }
  else if(lhs.id() == ID_extractbit)
  {
    // one bit of a vector
    auto &extractbit = to_extractbit_expr(lhs);
    auto &vector = extractbit.src();
    auto vector_type = vector.type();
    auto width = to_bitvector_type(vector_type).get_width();
    auto old_value = current_value(vector, values);

    // we build the vector bit by bit, most significant first
    exprt::operandst bits;
    bits.reserve(width);

    for(std::size_t b = width; b > 0; b--)
    {
      auto bit_index = from_integer(b - 1, extractbit.index().type());
      exprt old_bit = extractbit_exprt{old_value, bit_index};
      old_bit.type() = rhs.type();
      bits.push_back(
        if_exprt{equal_exprt{extractbit.index(), bit_index}, rhs, old_bit});
    }

    exprt new_value;
    if(width == 1)
      new_value = typecast_exprt{bits.front(), vector_type};
    else
      new_value = concatenation_exprt{std::move(bits), vector_type};

    return assign_to(vector, simplify_expr(new_value, ns), values);
  }
  else if(lhs.id() == ID_extractbits)
  {
    // a slice of a vector; the bounds are constant
    auto &extractbits = to_extractbits_expr(lhs);
    auto &vector = extractbits.src();
    auto vector_type = vector.type();
    auto width = to_bitvector_type(vector_type).get_width();
    auto slice_width = to_bitvector_type(rhs.type()).get_width();
    auto lower =
      numeric_cast_v<std::size_t>(to_constant_expr(extractbits.index()));
    auto old_value = current_value(vector, values);

    exprt::operandst parts;

    // the bits above the slice
    if(lower + slice_width < width)
    {
      auto upper_type = unsignedbv_typet{width - lower - slice_width};
      parts.push_back(
        extractbits_exprt{old_value, lower + slice_width, upper_type});
    }

    parts.push_back(rhs);

    // the bits below the slice
    if(lower > 0)
      parts.push_back(extractbits_exprt{old_value, 0, unsignedbv_typet{lower}});

    exprt new_value;
    if(parts.size() == 1)
      new_value = typecast_exprt{parts.front(), vector_type};
    else
      new_value = concatenation_exprt{std::move(parts), vector_type};

    return assign_to(vector, std::move(new_value), values);
  }
  else if(lhs.id() == ID_index)
  {
    auto &index = to_index_expr(lhs);
    auto old_value = current_value(index.array(), values);
    auto new_value = with_exprt{old_value, index.index(), rhs};
    return assign_to(index.array(), std::move(new_value), values);
  }
  else if(lhs.id() == ID_member)
  {
    auto &member = to_member_expr(lhs);
    auto old_value = current_value(member.compound(), values);
    exprt where(ID_member_name);
    where.set(ID_component_name, member.get_component_name());
    auto new_value = with_exprt{old_value, where, rhs};
    return assign_to(member.compound(), std::move(new_value), values);
  }
  else
  {
    error().source_location = lhs.source_location();
    error() << "unexpected assignment target: " << lhs.id() << eom;
    throw 0;
  }
}

void vhdl_synthesist::execute_assign(
  const codet &code,
  const exprt &guard,
  valuest &values,
  bool clocked)
{
  auto lhs = substitute_indices(code.op0(), values);
  auto &rhs = code.op1();

  // find the object
  exprt base = lhs;
  while(base.id() != ID_symbol)
    base = base.operands().front();

  auto &symbol_expr = to_symbol_expr(base);
  auto &symbol = lookup(symbol_expr);

  // s <= unaffected;
  if(rhs.is_nil())
    return;

  // IEEE 1076-2008 6.4.2.3: one driver per signal; the resolution
  // of multiple drivers is not supported
  auto driver_entry = drivers.emplace(symbol.name, current_driver);
  if(!driver_entry.second && driver_entry.first->second != current_driver)
  {
    error().source_location = code.source_location();
    error() << "`" << symbol.base_name << "' has multiple drivers" << eom;
    throw 0;
  }

  // The value is an expression, or nested if expressions with a
  // nil else branch when there is none (the VHDL-2008 conditional
  // assignment). A nil means the object is unaffected.
  std::function<exprt(const exprt &)> value_rec = [&](const exprt &v) -> exprt
  {
    if(v.id() == ID_if)
    {
      auto &if_expr = to_if_expr(v);

      // IEEE 1076.6-2004 6.1.3.3: q <= d when rising_edge(clk);
      if(contains_clock_edge(if_expr.cond()))
      {
        if(!is_clock_edge(if_expr.cond()) || !clocked)
        {
          error().source_location = if_expr.source_location();
          error() << "unsupported clock edge condition" << eom;
          throw 0;
        }

        if(if_expr.false_case().is_not_nil())
        {
          error().source_location = if_expr.source_location();
          error() << "else branch on a clock edge condition" << eom;
          throw 0;
        }

        return value_rec(if_expr.true_case());
      }

      auto cond = substitute(if_expr.cond(), values);
      auto true_case = value_rec(if_expr.true_case());
      auto false_case = if_expr.false_case().is_nil()
                          ? value_of(symbol_expr, values)
                          : value_rec(if_expr.false_case());
      return if_exprt{
        std::move(cond), std::move(true_case), std::move(false_case)};
    }
    else
      return assign_to(lhs, substitute(v, values), values);
  };

  auto new_value = value_rec(rhs);

  // apply the guard
  if(!guard.is_true())
    new_value = if_exprt{guard, new_value, value_of(symbol_expr, values)};

  values[symbol.name] = simplify_expr(new_value, ns);
}

void vhdl_synthesist::execute(
  const codet &code,
  const exprt &guard,
  valuest &values,
  bool clocked)
{
  auto statement = code.get_statement();

  if(statement == ID_block)
  {
    for(auto &s : to_code_block(code).statements())
      execute(s, guard, values, clocked);
  }
  else if(statement == ID_skip)
  {
  }
  else if(statement == ID_assign || statement == ID_vhdl_signal_assign)
  {
    execute_assign(code, guard, values, clocked);
  }
  else if(statement == ID_ifthenelse)
  {
    auto &if_code = to_code_ifthenelse(code);
    auto cond = if_code.cond();

    if(contains_clock_edge(cond))
    {
      if(!is_clock_edge(cond))
      {
        error().source_location = cond.source_location();
        error() << "unsupported clock edge condition" << eom;
        throw 0;
      }

      if(!clocked)
      {
        error().source_location = cond.source_location();
        error() << "clock edge in a combinational process" << eom;
        throw 0;
      }

      // IEEE 1076.6-2004 6.1.3.1: no else branch on the clock edge
      if(if_code.has_else_case())
      {
        error().source_location = cond.source_location();
        error() << "else branch on a clock edge condition" << eom;
        throw 0;
      }

      // The body is executed when the clock ticks, which is
      // every cycle.
      execute(if_code.then_case(), guard, values, clocked);
      return;
    }

    cond = substitute(cond, values);

    // execute both branches
    valuest then_values = values, else_values = values;

    execute(
      if_code.then_case(),
      guard.is_true() ? cond : exprt{and_exprt{guard, cond}},
      then_values,
      clocked);

    if(if_code.has_else_case())
    {
      exprt not_cond = not_exprt{cond};
      execute(
        if_code.else_case(),
        guard.is_true() ? not_cond : exprt{and_exprt{guard, not_cond}},
        else_values,
        clocked);
    }

    // merge
    for(auto &entry : then_values)
      values[entry.first] = entry.second;

    for(auto &entry : else_values)
    {
      auto v_it = then_values.find(entry.first);
      if(v_it == then_values.end())
        values[entry.first] = entry.second;
      else if(v_it->second != entry.second)
        values[entry.first] =
          simplify_expr(if_exprt{cond, v_it->second, entry.second}, ns);
    }
  }
  else if(statement == ID_assert)
  {
    auto condition = substitute(code.op0(), values);
    if(!guard.is_true())
      condition = implies_exprt{guard, condition};

    synth_property(
      condition,
      code.get(ID_label),
      static_cast<const exprt &>(code.find(ID_report)),
      code.source_location());
  }
  else
  {
    error().source_location = code.source_location();
    error() << "unexpected statement during synthesis: " << statement << eom;
    throw 0;
  }
}

void vhdl_synthesist::synth_property(
  const exprt &condition,
  const irep_idt &label,
  const exprt &report,
  const source_locationt &source_location)
{
  symbolt new_symbol;

  if(label.empty())
    new_symbol.base_name = "property" + std::to_string(++property_counter);
  else
    new_symbol.base_name = label;

  new_symbol.name =
    id2string(module_symbol->name) + "." + id2string(new_symbol.base_name);
  new_symbol.pretty_name = id2string(module_symbol->pretty_name) + "." +
                           id2string(new_symbol.base_name);
  new_symbol.is_property = true;
  new_symbol.mode = ID_VHDL;
  new_symbol.type = bool_typet();
  new_symbol.module = module_symbol->name;
  new_symbol.location = source_location;

  // there is an implicit 'always'
  new_symbol.value = G_exprt{condition};

  if(report.id() == ID_constant && report.type().id() == ID_string)
  {
    new_symbol.location.set_comment(
      "assertion " + id2string(to_constant_expr(report).get_value()));
  }
  else
    new_symbol.location.set_comment("assertion");

  if(symbol_table.add(new_symbol))
  {
    error().source_location = source_location;
    error() << "duplicate property `" << new_symbol.base_name << "'" << eom;
    throw 0;
  }
}

void vhdl_synthesist::synth_statement(const codet &code, bool clocked)
{
  current_driver++;

  valuest values;
  execute(code, true_exprt{}, values, clocked);

  for(auto &entry : values)
  {
    auto &symbol = symbol_table.get_writeable_ref(entry.first);
    symbol_exprt symbol_expr{symbol.name, symbol.type};

    check_no_clock_edge(entry.second);

    if(clocked)
    {
      // Variables and signals assigned in a clocked process are
      // state variables with a next-state function.
      symbol.is_state_var = true;
      symbol.is_input = false; // for inout ports
      trans.push_back(
        equal_exprt{next_symbol_exprt{symbol_expr}, entry.second});
    }
    else
    {
      // combinational
      invar.push_back(equal_exprt{symbol_expr, entry.second});
    }
  }
}

void vhdl_synthesist::synth_process(const exprt &process)
{
  // the body of the process is operand 0
  auto &body = to_code(to_unary_expr(process).op());
  synth_statement(body, contains_clock_edge(body));
}

void vhdl_synthesist::synth_module(const irept &module)
{
  PRECONDITION(module.id() == ID_module);

  const exprt &body = static_cast<const exprt &>(module.find(ID_body));

  for(auto &op : body.operands())
  {
    if(op.id() == ID_process)
      synth_process(op);
    else if(op.id() == ID_code)
    {
      auto &code = to_code(op);
      auto statement = code.get_statement();

      if(statement == ID_assert)
      {
        // a concurrent assertion
        synth_property(
          code.op0(),
          code.get(ID_label),
          static_cast<const exprt &>(code.find(ID_report)),
          code.source_location());
      }
      else
      {
        // concurrent signal assignments, possibly selected or
        // conditional; the condition may be a clock edge
        synth_statement(code, contains_clock_edge(code));
      }
    }
    else
    {
      error().source_location = op.source_location();
      error() << "unexpected module item during synthesis: " << op.id() << eom;
      throw 0;
    }
  }

  // Initial values, and the state variables that are not assigned
  // by any clocked process or combinational statement hold their value.
  for(auto &entry : symbol_table.symbols)
  {
    auto &symbol = entry.second;

    if(symbol.module != module_symbol->name || !symbol.is_state_var)
      continue;

    symbol_exprt symbol_expr{symbol.name, symbol.type};

    if(symbol.value.is_not_nil())
    {
      check_no_clock_edge(symbol.value);
      init.push_back(equal_exprt{symbol_expr, symbol.value});
    }

    if(drivers.find(symbol.name) == drivers.end())
      trans.push_back(equal_exprt{next_symbol_exprt{symbol_expr}, symbol_expr});
  }

  for(auto &c : invar)
    check_no_clock_edge(c);
}

bool vhdl_synthesist::operator()()
{
  try
  {
    auto s_it = symbol_table.get_writeable(module);

    if(s_it == nullptr)
    {
      error() << "module `" << module << "' not found" << eom;
      throw 0;
    }

    symbolt &symbol = *s_it;
    module_symbol = &symbol;

    property_counter = 0;

    synth_module(symbol.value);

    transt trans_expr{
      ID_trans,
      conjunction(invar),
      conjunction(init),
      conjunction(trans),
      symbol.type};

    symbol.value = trans_expr;

    return false;
  }
  catch(int)
  {
    return true;
  }
}

bool vhdl_synthesis(
  symbol_table_baset &symbol_table,
  const irep_idt &module,
  message_handlert &message_handler)
{
  return vhdl_synthesist(symbol_table, module, message_handler)();
}
