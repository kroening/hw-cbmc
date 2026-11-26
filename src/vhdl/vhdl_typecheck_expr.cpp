/*******************************************************************\

Module: VHDL Type Checking of Expressions

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

/// \file
/// VHDL Type Checking of Expressions

#include "vhdl_typecheck_class.h"

#include <util/arith_tools.h>
#include <util/bitvector_expr.h>
#include <util/mathematical_expr.h>
#include <util/simplify_expr.h>

#include <algorithm>

exprt vhdl_typecheckt::typecheck_expr(exprt expr)
{
  auto source_location = expr.source_location();
  auto result = typecheck_expr_rec(std::move(expr));
  if(result.source_location().is_nil())
    result.add_source_location() = source_location;
  return result;
}

exprt vhdl_typecheckt::typecheck_expr_rec(exprt expr)
{
  if(expr.id() == ID_symbol)
  {
    return typecheck_symbol(to_symbol_expr(expr));
  }
  else if(expr.id() == ID_constant)
  {
    // The literals are universal, and get their type from the context;
    // this is done in convert_to.
    auto type_id = expr.type().id();

    if(
      type_id == ID_natural || type_id == ID_char || type_id == ID_string ||
      type_id == ID_vhdl_bit_string || type_id == ID_real)
    {
      return expr;
    }
    else if(type_id == ID_nullptr)
      throw errort{}.with_location(expr.source_location())
        << "null literals are not supported";
    else
      return expr; // already typed
  }
  else if(expr.id() == ID_vhdl_physical)
  {
    throw errort{}.with_location(expr.source_location())
      << "physical literals are not supported";
  }
  else if(expr.id() == ID_vhdl_aggregate)
  {
    // typed in convert_to
    expr.type() = typet{ID_vhdl_aggregate};
    return expr;
  }
  else if(expr.id() == ID_vhdl_indexed)
  {
    return typecheck_indexed(std::move(expr));
  }
  else if(expr.id() == ID_vhdl_slice)
  {
    return typecheck_slice(std::move(expr));
  }
  else if(expr.id() == ID_vhdl_attribute)
  {
    return typecheck_attribute(std::move(expr));
  }
  else if(expr.id() == ID_vhdl_qualified)
  {
    auto &binary = to_binary_expr(expr);
    auto type = typecheck_type_mark(binary.op0());
    return convert_to(typecheck_expr(binary.op1()), type);
  }
  else if(expr.id() == ID_member)
  {
    auto &op = to_unary_expr(expr).op();
    auto prefix = typecheck_expr(op);
    irep_idt component_name = to_lower(expr.get(ID_component_name));

    if(prefix.type().id() != ID_struct)
      throw errort{}.with_location(expr.source_location())
        << "selected name requires a record";

    auto &struct_type = to_struct_type(prefix.type());

    if(!struct_type.has_component(component_name))
      throw errort{}.with_location(expr.source_location())
        << "record has no element `" << component_name << "'";

    auto &component = struct_type.get_component(component_name);
    return member_exprt{std::move(prefix), component_name, component.type()};
  }
  else if(
    expr.id() == ID_and || expr.id() == ID_or || expr.id() == ID_nand ||
    expr.id() == ID_nor || expr.id() == ID_xor || expr.id() == ID_xnor ||
    expr.id() == ID_equal || expr.id() == ID_notequal || expr.id() == ID_lt ||
    expr.id() == ID_le || expr.id() == ID_gt || expr.id() == ID_ge ||
    expr.id() == ID_plus || expr.id() == ID_minus || expr.id() == ID_mult ||
    expr.id() == ID_div || expr.id() == ID_mod || expr.id() == ID_vhdl_rem ||
    expr.id() == ID_power || expr.id() == ID_concatenation ||
    expr.id() == ID_shl || expr.id() == ID_lshr || expr.id() == ID_ashr ||
    expr.id() == ID_rol || expr.id() == ID_ror)
  {
    return typecheck_binary(std::move(expr));
  }
  else if(
    expr.id() == ID_not || expr.id() == ID_unary_minus ||
    expr.id() == ID_unary_plus || expr.id() == ID_abs ||
    expr.id() == ID_typecast)
  {
    return typecheck_unary(std::move(expr));
  }
  else if(expr.id() == ID_if)
  {
    throw errort{}.with_location(expr.source_location())
      << "conditional expressions are only allowed in assignments";
  }
  else if(expr.id() == ID_range)
  {
    throw errort{}.with_location(expr.source_location()) << "unexpected range";
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "unexpected expression: " << expr.id();
}

exprt vhdl_typecheckt::typecheck_symbol(symbol_exprt symbol_expr)
{
  irep_idt identifier = symbol_expr.get_identifier();
  irep_idt lower_case_identifier = to_lower(identifier);

  // enumeration literals, including true and false
  if(auto literal = lookup_enumeration_literal(lower_case_identifier))
  {
    exprt result = *literal;
    result.add_source_location() = symbol_expr.source_location();
    return result;
  }

  // constants are inlined
  if(auto constant = lookup_constant(lower_case_identifier))
  {
    exprt result = *constant;
    result.add_source_location() = symbol_expr.source_location();
    return result;
  }

  // signals, ports, variables
  if(auto symbol = lookup_object(lower_case_identifier))
  {
    symbol_exprt result{symbol->name, symbol->type};
    result.add_source_location() = symbol_expr.source_location();
    return std::move(result);
  }

  throw errort{}.with_location(symbol_expr.source_location())
    << "`" << identifier << "' is not declared";
}

/// IEEE 1076-2008 8.4: the bit position of an index into a vector,
/// counting from the right-most bit, which is bit 0
exprt vhdl_typecheckt::vector_bit_index(
  const exprt &index,
  const typet &vector_type)
{
  auto range = get_vhdl_range(vector_type);
  auto index_integer = convert_to(index, vhdl_integer_type());

  // downto: the bit is index - right
  // to: the bit is right - index
  exprt result;
  if(range.direction == vhdl_directiont::DOWNTO)
    result = minus_exprt{
      index_integer, from_integer(range.right, vhdl_integer_type())};
  else
    result = minus_exprt{
      from_integer(range.right, vhdl_integer_type()), index_integer};

  return simplify_expr(result, ns);
}

/// An indexed name, a function call, or a type conversion; these
/// cannot be distinguished syntactically.
exprt vhdl_typecheckt::typecheck_indexed(exprt expr)
{
  auto &operands = expr.operands();
  DATA_INVARIANT(!operands.empty(), "indexed names have a prefix");

  auto &prefix = operands.front();

  // the arguments are association elements
  exprt::operandst arguments;
  for(std::size_t i = 1; i < operands.size(); i++)
  {
    auto &association = operands[i];
    if(association.find(ID_formal).is_not_nil())
      throw errort{}.with_location(association.source_location())
        << "named association is not supported in calls";
    arguments.push_back(
      static_cast<const exprt &>(association.find(ID_actual)));
  }

  if(prefix.id() == ID_symbol)
  {
    irep_idt lower_case_name = to_lower(prefix.get(ID_identifier));

    // a type conversion?
    if(auto type = lookup_type(lower_case_name))
    {
      if(arguments.size() != 1)
        throw errort{}.with_location(expr.source_location())
          << "type conversion takes one argument";

      return typecheck_type_conversion(
        typecheck_expr(arguments.front()), *type);
    }

    // a function call? Objects shadow functions.
    if(
      lookup_object(lower_case_name) == nullptr &&
      lookup_constant(lower_case_name) == nullptr)
    {
      return typecheck_function_call(
        lower_case_name, std::move(arguments), expr.source_location());
    }
  }

  // an indexed name
  auto prefix_typed = typecheck_expr(prefix);

  if(arguments.size() != 1)
    throw errort{}.with_location(expr.source_location())
      << "only one-dimensional arrays are supported";

  auto &argument = arguments.front();

  // the argument may be a range, e.g., v(x'range)
  if(argument.id() == ID_vhdl_attribute)
  {
    auto attribute = to_lower(argument.get(ID_identifier));
    if(attribute == "range" || attribute == "reverse_range")
    {
      exprt slice(ID_vhdl_slice);
      slice.add_source_location() = expr.source_location();
      slice.add_to_operands(exprt{prefix}, exprt{argument});
      return typecheck_slice(std::move(slice));
    }
  }

  auto &prefix_type = prefix_typed.type();

  if(is_vhdl_vector_type(prefix_type))
  {
    auto bit = vector_bit_index(typecheck_expr(argument), prefix_type);
    auto result = extractbit_exprt{std::move(prefix_typed), std::move(bit)};
    result.type() = vhdl_element_type(prefix_type);
    return std::move(result);
  }
  else if(prefix_type.id() == ID_array && has_vhdl_range(prefix_type))
  {
    auto range = get_vhdl_range(prefix_type);
    auto index = convert_to(typecheck_expr(argument), vhdl_integer_type());
    auto position = simplify_expr(
      minus_exprt{index, from_integer(range.low(), vhdl_integer_type())}, ns);
    return index_exprt{std::move(prefix_typed), std::move(position)};
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "indexing requires an array";
}

/// IEEE 1076-2008 8.5
exprt vhdl_typecheckt::typecheck_slice(exprt expr)
{
  auto &binary = to_binary_expr(expr);
  auto prefix = typecheck_expr(binary.op0());
  auto range = typecheck_range(binary.op1());

  auto &prefix_type = prefix.type();

  if(!is_vhdl_vector_type(prefix_type))
    throw errort{}.with_location(expr.source_location())
      << "slices are only supported on vectors";

  auto prefix_range = get_vhdl_range(prefix_type);

  if(range.direction != prefix_range.direction)
    throw errort{}.with_location(expr.source_location())
      << "the direction of a slice must match the direction of the vector";

  if(range.low() < prefix_range.low() || range.high() > prefix_range.high())
    throw errort{}.with_location(expr.source_location())
      << "slice out of range";

  // the lowest bit of the slice
  mp_integer lowest_bit = range.direction == vhdl_directiont::DOWNTO
                            ? range.low() - prefix_range.right
                            : prefix_range.right - range.high();

  typet result_type =
    prefix_type.id() == ID_signedbv
      ? vhdl_signed_vector_type(range)
      : vhdl_unsigned_vector_type(vhdl_type_name(prefix_type), range);

  return extractbits_exprt{
    std::move(prefix), numeric_cast_v<std::size_t>(lowest_bit), result_type};
}

/// IEEE 1076-2008 16.2: predefined attributes
exprt vhdl_typecheckt::typecheck_attribute(exprt expr)
{
  auto &prefix = to_unary_expr(expr).op();
  irep_idt attribute = to_lower(expr.get(ID_identifier));

  // the prefix may be a type or an object
  typet prefix_type;
  std::optional<exprt> prefix_expr;

  const typet *type_ptr = prefix.id() == ID_symbol
                            ? lookup_type(to_lower(prefix.get(ID_identifier)))
                            : nullptr;

  if(type_ptr != nullptr)
    prefix_type = *type_ptr;
  else
  {
    prefix_expr = typecheck_expr(prefix);
    prefix_type = prefix_expr->type();
  }

  if(attribute == "event")
  {
    if(!prefix_expr.has_value() || prefix_expr->id() != ID_symbol)
      throw errort{}.with_location(expr.source_location())
        << "'event requires a signal";

    return unary_exprt{ID_vhdl_event, *prefix_expr, vhdl_boolean_type()};
  }
  else if(
    attribute == "length" || attribute == "left" || attribute == "right" ||
    attribute == "high" || attribute == "low")
  {
    if(!has_vhdl_range(prefix_type))
      throw errort{}.with_location(expr.source_location())
        << "'" << attribute << " requires an array or a range";

    auto range = get_vhdl_range(prefix_type);
    mp_integer value;

    if(attribute == "length")
      value = range.length();
    else if(attribute == "left")
      value = range.left;
    else if(attribute == "right")
      value = range.right;
    else if(attribute == "high")
      value = range.high();
    else
      value = range.low();

    return from_integer(value, vhdl_integer_type());
  }
  else if(attribute == "range" || attribute == "reverse_range")
  {
    throw errort{}.with_location(expr.source_location())
      << "range attribute outside of a range context";
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "unsupported attribute `" << attribute << "'";
}

/// The functions of std_logic_1164, numeric_std, std_logic_arith and
/// std_logic_unsigned that we support.
exprt vhdl_typecheckt::typecheck_function_call(
  const irep_idt &name,
  exprt::operandst arguments,
  const source_locationt &source_location)
{
  auto expect_arguments = [&](std::size_t n)
  {
    if(arguments.size() != n)
      throw errort{}.with_location(source_location)
        << "function `" << name << "' expects " << n << " argument(s)";
  };

  if(name == "rising_edge" || name == "falling_edge")
  {
    expect_arguments(1);
    auto signal = typecheck_expr(arguments[0]);

    if(signal.id() != ID_symbol || !is_vhdl_scalar_bit_type(signal.type()))
      throw errort{}.with_location(source_location)
        << name << " requires a signal of type std_logic or bit";

    return unary_exprt{
      name == "rising_edge" ? ID_vhdl_rising_edge : ID_vhdl_falling_edge,
      std::move(signal),
      vhdl_boolean_type()};
  }
  else if(name == "to_integer" || name == "conv_integer")
  {
    expect_arguments(1);
    auto vector = typecheck_expr(arguments[0]);

    if(!is_vhdl_vector_type(vector.type()))
      throw errort{}.with_location(source_location)
        << name << " requires a vector";

    return typecast_exprt{std::move(vector), vhdl_integer_type()};
  }
  else if(
    name == "to_unsigned" || name == "to_signed" ||
    name == "conv_std_logic_vector" || name == "conv_unsigned" ||
    name == "conv_signed")
  {
    expect_arguments(2);
    auto value = typecheck_expr(arguments[0]);
    auto width = typecheck_static_integer(arguments[1]);

    if(width <= 0)
      throw errort{}.with_location(source_location) << "width must be positive";

    vhdl_ranget range{width - 1, 0, vhdl_directiont::DOWNTO};

    typet result_type;
    if(name == "to_signed" || name == "conv_signed")
      result_type = vhdl_signed_vector_type(range);
    else if(name == "conv_std_logic_vector")
      result_type = vhdl_unsigned_vector_type("std_logic_vector", range);
    else
      result_type = vhdl_unsigned_vector_type("unsigned", range);

    if(is_vhdl_universal_type(value.type()))
      value = convert_to(std::move(value), vhdl_integer_type());

    if(
      !is_vhdl_integer_type(value.type()) && !is_vhdl_vector_type(value.type()))
      throw errort{}.with_location(source_location)
        << name << " requires an integer or a vector";

    return typecast_exprt{std::move(value), result_type};
  }
  else if(name == "resize")
  {
    expect_arguments(2);
    auto value = typecheck_expr(arguments[0]);
    auto width = typecheck_static_integer(arguments[1]);

    if(!is_vhdl_vector_type(value.type()))
      throw errort{}.with_location(source_location)
        << "resize requires a vector";

    vhdl_ranget range{width - 1, 0, vhdl_directiont::DOWNTO};
    typet result_type =
      value.type().id() == ID_signedbv
        ? vhdl_signed_vector_type(range)
        : vhdl_unsigned_vector_type(vhdl_type_name(value.type()), range);

    auto old_width = get_vhdl_range(value.type()).length();

    if(value.type().id() == ID_signedbv && width < old_width && width >= 1)
    {
      // numeric_std keeps the sign bit and the rightmost width-1 bits
      auto new_width = numeric_cast_v<std::size_t>(width);
      exprt sign_bit =
        extractbit_exprt{value, numeric_cast_v<std::size_t>(old_width - 1)};
      if(new_width == 1)
        return typecast_exprt{std::move(sign_bit), result_type};
      exprt low_bits =
        extractbits_exprt{value, 0, unsignedbv_typet{new_width - 1}};
      return concatenation_exprt{
        std::move(sign_bit), std::move(low_bits), result_type};
    }

    return typecast_exprt{std::move(value), result_type};
  }
  else if(name == "shift_left" || name == "shift_right")
  {
    expect_arguments(2);
    auto value = typecheck_expr(arguments[0]);
    auto amount = convert_to(typecheck_expr(arguments[1]), vhdl_integer_type());

    if(!is_vhdl_vector_type(value.type()))
      throw errort{}.with_location(source_location)
        << name << " requires a vector";

    if(name == "shift_left")
      return shl_exprt{std::move(value), std::move(amount)};
    else if(value.type().id() == ID_signedbv)
      return ashr_exprt{std::move(value), std::move(amount)};
    else
      return lshr_exprt{std::move(value), std::move(amount)};
  }
  else
    throw errort{}.with_location(source_location)
      << "unknown function `" << name << "'";
}

/// A typecast, unless the types are the same modulo the VHDL type
/// name and range, in which case the type is adjusted.
static exprt retype(exprt expr, const typet &type)
{
  if(expr.type() == type)
    expr.type() = type;
  else
    expr = typecast_exprt{std::move(expr), type};
  return expr;
}

/// IEEE 1076-2008 9.3.6: type conversion
exprt vhdl_typecheckt::typecheck_type_conversion(exprt expr, const typet &type)
{
  if(is_vhdl_universal_type(expr.type()))
    return convert_to(std::move(expr), type);

  auto &source_type = expr.type();

  if(type.id() == ID_unconstrained)
  {
    // e.g., std_logic_vector(u); the range is that of the operand
    if(!is_vhdl_vector_type(source_type))
      throw errort{}.with_location(expr.source_location())
        << "conversion to " << vhdl_type_name(type) << " requires a vector";

    auto range = get_vhdl_range(source_type);
    typet result_type =
      vhdl_type_name(type) == "signed"
        ? vhdl_signed_vector_type(range)
        : vhdl_unsigned_vector_type(vhdl_type_name(type), range);

    return retype(std::move(expr), result_type);
  }
  else if(is_vhdl_integer_type(type))
  {
    if(!is_vhdl_integer_type(source_type))
      throw errort{}.with_location(expr.source_location())
        << "conversion to integer requires an integer";

    return retype(std::move(expr), type);
  }
  else if(is_vhdl_vector_type(type))
  {
    if(
      !is_vhdl_vector_type(source_type) ||
      get_vhdl_range(type).length() != get_vhdl_range(source_type).length())
      throw errort{}.with_location(expr.source_location())
        << "conversion to " << vhdl_type_name(type)
        << " requires a vector of the same length";

    return retype(std::move(expr), type);
  }
  else if(is_vhdl_scalar_bit_type(type))
  {
    if(!is_vhdl_scalar_bit_type(source_type))
      throw errort{}.with_location(expr.source_location())
        << "conversion to " << vhdl_type_name(type) << " requires a bit";

    expr.type() = type;
    return expr;
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "unsupported type conversion";
}

/// The value of a bit in a character or string literal
static std::optional<bool> bit_value(char ch)
{
  switch(ch)
  {
  case '0':
  case 'L':
  case 'l':
    return false;
  case '1':
  case 'H':
  case 'h':
    return true;
  default:
    return {};
  }
}

/// The bits of a bit string literal (IEEE 1076-2008 15.8),
/// most significant first
static std::optional<std::vector<bool>>
bit_string_bits(const std::string &literal)
{
  std::vector<bool> bits;
  std::size_t i = 0;

  // optional length is not supported
  if(i < literal.size() && isdigit(literal[i]))
    return {};

  if(i >= literal.size())
    return {};

  char base_ch = tolower(literal[i]);
  i++;

  unsigned bits_per_digit;
  if(base_ch == 'b')
    bits_per_digit = 1;
  else if(base_ch == 'o')
    bits_per_digit = 3;
  else if(base_ch == 'x')
    bits_per_digit = 4;
  else
    return {};

  // skip the quote
  if(i >= literal.size() || literal[i] != '"')
    return {};
  i++;

  for(; i < literal.size() && literal[i] != '"'; i++)
  {
    char ch = literal[i];
    if(ch == '_')
      continue;

    unsigned digit;
    if(isdigit(ch))
      digit = ch - '0';
    else if(isxdigit(ch))
      digit = tolower(ch) - 'a' + 10;
    else
      return {};

    if(digit >= (1u << bits_per_digit))
      return {};

    for(unsigned b = bits_per_digit; b > 0; b--)
      bits.push_back((digit >> (b - 1)) & 1);
  }

  return bits;
}

/// Build a vector constant from bits, most significant first
static constant_exprt
bits_to_constant(const std::vector<bool> &bits, const typet &type)
{
  mp_integer value = 0;
  for(bool b : bits)
    value = value * 2 + (b ? 1 : 0);
  return from_integer(value, type);
}

/// IEEE 1076-2008 9.3.3: aggregates
exprt vhdl_typecheckt::typecheck_aggregate(exprt aggregate, const typet &type)
{
  auto &associations = aggregate.operands();

  if(is_vhdl_vector_type(type))
  {
    auto range = get_vhdl_range(type);
    auto width = numeric_cast_v<std::size_t>(range.length());
    auto element_type = vhdl_element_type(type);

    // the bits, from the right-most, bit 0; the bit position of
    // VHDL index i is i - right for downto, and right - i for to
    std::vector<std::optional<exprt>> bits(width);
    std::optional<exprt> others;

    auto bit_position = [&range](const mp_integer &index) -> std::size_t
    {
      return numeric_cast_v<std::size_t>(
        range.direction == vhdl_directiont::DOWNTO ? index - range.right
                                                   : range.right - index);
    };

    std::size_t positional = 0;

    for(auto &association : associations)
    {
      auto value = convert_to(
        typecheck_expr(to_unary_expr(association).op()), element_type);
      auto &choices = association.find(ID_choices).get_sub();

      if(choices.empty())
      {
        // positional, left to right
        if(positional >= width)
          throw errort{}.with_location(association.source_location())
            << "too many elements in aggregate";
        mp_integer index = range.direction == vhdl_directiont::TO
                             ? range.left + positional
                             : range.left - positional;
        bits[bit_position(index)] = value;
        positional++;
      }
      else
      {
        for(auto &choice : choices)
        {
          auto &choice_expr = static_cast<const exprt &>(choice);
          if(choice_expr.id() == ID_others)
            others = value;
          else if(choice_expr.id() == ID_range)
          {
            auto choice_range = typecheck_range(choice_expr);
            for(mp_integer i = choice_range.low(); i <= choice_range.high();
                ++i)
            {
              if(i < range.low() || i > range.high())
                throw errort{}.with_location(choice_expr.source_location())
                  << "aggregate index out of range";
              bits[bit_position(i)] = value;
            }
          }
          else
          {
            auto i = typecheck_static_integer(choice_expr);
            if(i < range.low() || i > range.high())
              throw errort{}.with_location(choice_expr.source_location())
                << "aggregate index out of range";
            bits[bit_position(i)] = value;
          }
        }
      }
    }

    // assemble, most significant bit first
    exprt::operandst bit_operands;
    for(std::size_t b = width; b > 0; b--)
    {
      auto &bit = bits[b - 1];
      if(bit.has_value())
        bit_operands.push_back(*bit);
      else if(others.has_value())
        bit_operands.push_back(*others);
      else
        throw errort{}.with_location(aggregate.source_location())
          << "aggregate does not cover all elements";
    }

    exprt result;
    if(width == 1)
      result = typecast_exprt{bit_operands.front(), type};
    else
      result = concatenation_exprt{std::move(bit_operands), type};

    return simplify_expr(result, ns);
  }
  else if(type.id() == ID_array && has_vhdl_range(type))
  {
    auto &array_type = to_array_type(type);
    auto range = get_vhdl_range(type);
    auto size = numeric_cast_v<std::size_t>(range.length());

    std::vector<std::optional<exprt>> elements(size);
    std::optional<exprt> others;
    std::size_t positional = 0;

    for(auto &association : associations)
    {
      auto value = convert_to(
        typecheck_expr(to_unary_expr(association).op()),
        array_type.element_type());
      auto &choices = association.find(ID_choices).get_sub();

      if(choices.empty())
      {
        if(positional >= size)
          throw errort{}.with_location(association.source_location())
            << "too many elements in aggregate";
        mp_integer index = range.direction == vhdl_directiont::TO
                             ? range.left + positional
                             : range.left - positional;
        elements[numeric_cast_v<std::size_t>(index - range.low())] = value;
        positional++;
      }
      else
      {
        for(auto &choice : choices)
        {
          auto &choice_expr = static_cast<const exprt &>(choice);
          if(choice_expr.id() == ID_others)
            others = value;
          else if(choice_expr.id() == ID_range)
          {
            auto choice_range = typecheck_range(choice_expr);
            for(mp_integer i = choice_range.low(); i <= choice_range.high();
                ++i)
            {
              if(i < range.low() || i > range.high())
                throw errort{}.with_location(choice_expr.source_location())
                  << "aggregate index out of range";
              elements[numeric_cast_v<std::size_t>(i - range.low())] = value;
            }
          }
          else
          {
            auto i = typecheck_static_integer(choice_expr);
            if(i < range.low() || i > range.high())
              throw errort{}.with_location(choice_expr.source_location())
                << "aggregate index out of range";
            elements[numeric_cast_v<std::size_t>(i - range.low())] = value;
          }
        }
      }
    }

    // only others?
    if(associations.size() == 1 && others.has_value())
      return array_of_exprt{*others, array_type};

    exprt::operandst element_operands;
    for(auto &element : elements)
    {
      if(element.has_value())
        element_operands.push_back(*element);
      else if(others.has_value())
        element_operands.push_back(*others);
      else
        throw errort{}.with_location(aggregate.source_location())
          << "aggregate does not cover all elements";
    }

    return array_exprt{std::move(element_operands), array_type};
  }
  else if(type.id() == ID_struct)
  {
    auto &struct_type = to_struct_type(type);
    auto &components = struct_type.components();

    std::vector<std::optional<exprt>> elements(components.size());
    std::size_t positional = 0;

    for(auto &association : associations)
    {
      auto &choices = association.find(ID_choices).get_sub();
      auto &value = to_unary_expr(association).op();

      if(choices.empty())
      {
        if(positional >= components.size())
          throw errort{}.with_location(association.source_location())
            << "too many elements in aggregate";
        elements[positional] =
          convert_to(typecheck_expr(value), components[positional].type());
        positional++;
      }
      else
      {
        for(auto &choice : choices)
        {
          auto &choice_expr = static_cast<const exprt &>(choice);
          if(choice_expr.id() == ID_others)
          {
            for(std::size_t i = 0; i < components.size(); i++)
              if(!elements[i].has_value())
                elements[i] =
                  convert_to(typecheck_expr(value), components[i].type());
          }
          else if(choice_expr.id() == ID_symbol)
          {
            auto name = to_lower(choice_expr.get(ID_identifier));
            bool found = false;
            for(std::size_t i = 0; i < components.size(); i++)
              if(components[i].get_name() == name)
              {
                elements[i] =
                  convert_to(typecheck_expr(value), components[i].type());
                found = true;
              }
            if(!found)
              throw errort{}.with_location(choice_expr.source_location())
                << "record has no element `" << name << "'";
          }
          else
            throw errort{}.with_location(choice_expr.source_location())
              << "unexpected choice in record aggregate";
        }
      }
    }

    exprt::operandst element_operands;
    for(std::size_t i = 0; i < components.size(); i++)
    {
      if(!elements[i].has_value())
        throw errort{}.with_location(aggregate.source_location())
          << "aggregate does not cover element `" << components[i].get_name()
          << "'";
      element_operands.push_back(*elements[i]);
    }

    return struct_exprt{std::move(element_operands), type};
  }
  else
    throw errort{}.with_location(aggregate.source_location())
      << "aggregate requires an array or record type";
}

/// Implicit conversion of a universal literal to the given type, and
/// type matching for typed expressions.
exprt vhdl_typecheckt::convert_to(exprt expr, const typet &type)
{
  auto &source_type = expr.type();

  if(vhdl_types_compatible(source_type, type))
  {
    // The comments, i.e., the VHDL type name and range, may differ,
    // e.g., for integer subtypes; we adopt those of the target type.
    expr.type() = type;
    return expr;
  }

  if(source_type.id() == ID_natural)
  {
    // decimal or based integer literal
    std::string value_string = expr.get_string(ID_value);
    mp_integer value;

    // remove underscores
    value_string.erase(
      std::remove(value_string.begin(), value_string.end(), '_'),
      value_string.end());

    auto hash = value_string.find('#');
    if(hash != std::string::npos)
    {
      // based literal: base#digits#
      auto base = string2integer(value_string.substr(0, hash));
      auto last_hash = value_string.rfind('#');
      auto digits = value_string.substr(hash + 1, last_hash - hash - 1);
      if(digits.find('.') != std::string::npos)
        throw errort{}.with_location(expr.source_location())
          << "based real literals are not supported";
      value = string2integer(digits, numeric_cast_v<unsigned>(base));
      if(last_hash + 1 < value_string.size())
        throw errort{}.with_location(expr.source_location())
          << "exponents on based literals are not supported";
    }
    else
    {
      auto e = value_string.find_first_of("eE");
      if(e != std::string::npos)
      {
        auto mantissa = string2integer(value_string.substr(0, e));
        auto exponent = string2integer(value_string.substr(e + 1));
        value = mantissa * power(10, exponent);
      }
      else
        value = string2integer(value_string);
    }

    if(is_vhdl_integer_type(type))
      return from_integer(value, type);
    else if(is_vhdl_vector_type(type))
    {
      // numeric_std allows, e.g., unsigned + natural
      auto width = get_vhdl_range(type).length();
      if(value < 0 || value >= power(2, width))
        throw errort{}.with_location(expr.source_location())
          << "integer literal " << value << " does not fit into "
          << vhdl_type_name(type) << "(" << width << ")";
      return from_integer(value, type);
    }
    else
      throw errort{}.with_location(expr.source_location())
        << "integer literal where " << vhdl_type_name(type) << " is expected";
  }
  else if(source_type.id() == ID_char)
  {
    std::string value = expr.get_string(ID_value);
    DATA_INVARIANT(value.size() == 1, "character literals have one character");

    if(is_vhdl_scalar_bit_type(type))
    {
      auto bit = bit_value(value[0]);
      if(!bit.has_value())
        throw errort{}.with_location(expr.source_location())
          << "the value '" << value << "' is not supported";

      exprt result = *bit ? static_cast<exprt>(true_exprt{})
                          : static_cast<exprt>(false_exprt{});
      result.type() = type;
      result.add_source_location() = expr.source_location();
      return result;
    }
    else if(type.id() == ID_enumeration)
    {
      for(auto &element : to_enumeration_type(type).elements())
        if(element.id() == value)
          return constant_exprt{value, type};

      throw errort{}.with_location(expr.source_location())
        << "the type " << vhdl_type_name(type) << " has no literal '" << value
        << "'";
    }
    else
      throw errort{}.with_location(expr.source_location())
        << "character literal where " << vhdl_type_name(type) << " is expected";
  }
  else if(
    source_type.id() == ID_string || source_type.id() == ID_vhdl_bit_string)
  {
    if(!is_vhdl_vector_type(type))
      throw errort{}.with_location(expr.source_location())
        << "string literal where " << vhdl_type_name(type) << " is expected";

    std::vector<bool> bits;

    if(source_type.id() == ID_string)
    {
      for(char ch : expr.get_string(ID_value))
      {
        auto bit = bit_value(ch);
        if(!bit.has_value())
          throw errort{}.with_location(expr.source_location())
            << "the value '" << ch << "' is not supported";
        bits.push_back(*bit);
      }
    }
    else
    {
      auto bits_opt = bit_string_bits(expr.get_string(ID_value));
      if(!bits_opt.has_value())
        throw errort{}.with_location(expr.source_location())
          << "unsupported bit string literal";
      bits = std::move(*bits_opt);
    }

    auto width = numeric_cast_v<std::size_t>(get_vhdl_range(type).length());

    // IEEE 1076-2008 9.3.2: bit strings are zero-extended or truncated
    // to the length of the type; string literals must match
    if(source_type.id() == ID_string && bits.size() != width)
      throw errort{}.with_location(expr.source_location())
        << "string literal has " << bits.size() << " elements, but "
        << vhdl_type_name(type) << " has " << width;

    while(bits.size() < width)
      bits.insert(bits.begin(), false);

    while(bits.size() > width)
    {
      if(bits.front())
        throw errort{}.with_location(expr.source_location())
          << "bit string literal does not fit into " << width << " bits";
      bits.erase(bits.begin());
    }

    auto result = bits_to_constant(bits, type);
    result.add_source_location() = expr.source_location();
    return std::move(result);
  }
  else if(source_type.id() == ID_vhdl_aggregate)
  {
    return typecheck_aggregate(std::move(expr), type);
  }
  else if(source_type.id() == ID_real)
  {
    throw errort{}.with_location(expr.source_location())
      << "real literals are not supported";
  }

  // Both typed; VHDL is strongly typed, so these must match.
  throw errort{}.with_location(expr.source_location())
    << "type mismatch: expected " << vhdl_type_name(type) << ", got "
    << (vhdl_type_name(source_type).empty()
          ? id2string(source_type.id())
          : id2string(vhdl_type_name(source_type)));
}

/// Make the types of two operands match, converting universal
/// literals as needed.
void vhdl_typecheckt::match_types(exprt &a, exprt &b)
{
  bool a_universal = is_vhdl_universal_type(a.type());
  bool b_universal = is_vhdl_universal_type(b.type());

  if(a_universal && b_universal)
  {
    // Both are literals; pick a type. This is ambiguous in VHDL
    // when both bit and std_logic are visible; we use std_logic.
    auto id = a.type().id();
    if(id == ID_natural)
    {
      a = convert_to(std::move(a), vhdl_integer_type());
      b = convert_to(std::move(b), vhdl_integer_type());
    }
    else if(id == ID_char)
    {
      a = convert_to(std::move(a), vhdl_std_logic_type());
      b = convert_to(std::move(b), vhdl_std_logic_type());
    }
    else if(id == ID_string)
    {
      vhdl_ranget range{
        mp_integer(a.get_string(ID_value).size()) - 1,
        0,
        vhdl_directiont::DOWNTO};
      auto type = vhdl_unsigned_vector_type("std_logic_vector", range);
      a = convert_to(std::move(a), type);
      b = convert_to(std::move(b), type);
    }
    else
      throw errort{}.with_location(a.source_location())
        << "cannot determine the type of the operands";
  }
  else if(a_universal)
    a = convert_to(std::move(a), b.type());
  else if(b_universal)
    b = convert_to(std::move(b), a.type());
  else if(vhdl_types_compatible(a.type(), b.type()))
  {
    // match up the subtypes
    b.type() = a.type();
  }
  else
  {
    // numeric_std allows mixing vectors and integers
    if(is_vhdl_vector_type(a.type()) && is_vhdl_integer_type(b.type()))
      b = typecast_exprt{std::move(b), a.type()};
    else if(is_vhdl_integer_type(a.type()) && is_vhdl_vector_type(b.type()))
      a = typecast_exprt{std::move(a), b.type()};
    else if(is_vhdl_vector_type(a.type()) && is_vhdl_vector_type(b.type()))
    {
      // numeric_std extends the shorter operand
      auto a_width = get_vhdl_range(a.type()).length();
      auto b_width = get_vhdl_range(b.type()).length();
      if(a_width < b_width)
        a = typecast_exprt{std::move(a), b.type()};
      else
        b = typecast_exprt{std::move(b), a.type()};
    }
    else
      throw errort{}.with_location(a.source_location())
        << "type mismatch: " << vhdl_type_name(a.type()) << " vs. "
        << vhdl_type_name(b.type());
  }
}

exprt vhdl_typecheckt::typecheck_binary(exprt expr)
{
  auto &binary = to_binary_expr(expr);
  auto id = expr.id();

  auto lhs = typecheck_expr(binary.lhs());
  auto rhs = typecheck_expr(binary.rhs());

  if(id == ID_concatenation)
  {
    // The operands are vectors or elements;
    // the result is a vector (n-1 downto 0).
    auto element_width = [this](exprt &op) -> mp_integer
    {
      if(is_vhdl_universal_type(op.type()))
      {
        if(op.type().id() == ID_char)
          op = convert_to(std::move(op), vhdl_std_logic_type());
        else if(op.type().id() == ID_string)
        {
          vhdl_ranget range{
            mp_integer(op.get_string(ID_value).size()) - 1,
            0,
            vhdl_directiont::DOWNTO};
          op = convert_to(
            std::move(op),
            vhdl_unsigned_vector_type("std_logic_vector", range));
        }
        else
          throw errort{}.with_location(op.source_location())
            << "cannot concatenate this literal";
      }

      if(is_vhdl_scalar_bit_type(op.type()))
        return 1;
      else if(is_vhdl_vector_type(op.type()))
        return get_vhdl_range(op.type()).length();
      else
        throw errort{}.with_location(op.source_location())
          << "concatenation requires vectors or bits";
    };

    auto lhs_width = element_width(lhs);
    auto rhs_width = element_width(rhs);

    irep_idt type_name =
      is_vhdl_vector_type(lhs.type())       ? vhdl_type_name(lhs.type())
      : is_vhdl_vector_type(rhs.type())     ? vhdl_type_name(rhs.type())
      : vhdl_type_name(lhs.type()) == "bit" ? "bit_vector"
                                            : "std_logic_vector";

    vhdl_ranget range{lhs_width + rhs_width - 1, 0, vhdl_directiont::DOWNTO};
    auto result_type = vhdl_unsigned_vector_type(type_name, range);

    return concatenation_exprt{std::move(lhs), std::move(rhs), result_type};
  }
  else if(
    id == ID_shl || id == ID_lshr || id == ID_ashr || id == ID_rol ||
    id == ID_ror)
  {
    if(!is_vhdl_vector_type(lhs.type()))
      throw errort{}.with_location(expr.source_location())
        << "shift requires a vector";

    if(expr.get_bool(ID_vhdl_sla))
      throw errort{}.with_location(expr.source_location())
        << "sla is not supported";

    rhs = convert_to(std::move(rhs), vhdl_integer_type());
    return shift_exprt{std::move(lhs), id, std::move(rhs)};
  }

  // When a vector is compared with an integer, numeric_std compares at
  // full precision, whereas the arithmetic operators truncate to the
  // vector. We remember a type wide enough for both.
  std::optional<typet> wider_type;

  if(
    (id == ID_equal || id == ID_notequal || id == ID_lt || id == ID_le ||
     id == ID_gt || id == ID_ge) &&
    !is_vhdl_universal_type(lhs.type()) &&
    !is_vhdl_universal_type(rhs.type()) &&
    (is_vhdl_vector_type(lhs.type()) != is_vhdl_vector_type(rhs.type())) &&
    (is_vhdl_integer_type(lhs.type()) || is_vhdl_integer_type(rhs.type())))
  {
    auto &vector_type =
      is_vhdl_vector_type(lhs.type()) ? lhs.type() : rhs.type();
    auto width = to_bitvector_type(vector_type).get_width();
    // one extra bit for unsigned vectors to keep them non-negative
    std::size_t wide = std::max<std::size_t>(width + 1, 33);
    wider_type = signedbv_typet{wide};
  }

  if(wider_type.has_value())
  {
    lhs = typecast_exprt{std::move(lhs), *wider_type};
    rhs = typecast_exprt{std::move(rhs), *wider_type};
  }
  else
    match_types(lhs, rhs);

  auto &type = lhs.type();

  if(
    id == ID_and || id == ID_or || id == ID_nand || id == ID_nor ||
    id == ID_xor || id == ID_xnor)
  {
    if(type.id() == ID_bool)
    {
      exprt result;
      if(id == ID_and)
        result = and_exprt{lhs, rhs};
      else if(id == ID_or)
        result = or_exprt{lhs, rhs};
      else if(id == ID_nand)
        result = not_exprt{and_exprt{lhs, rhs}};
      else if(id == ID_nor)
        result = not_exprt{or_exprt{lhs, rhs}};
      else if(id == ID_xor)
        result = xor_exprt{lhs, rhs};
      else
        result = not_exprt{xor_exprt{lhs, rhs}};
      result.type() = type;
      return result;
    }
    else if(is_vhdl_vector_type(type))
    {
      exprt result;
      if(id == ID_and)
        result = bitand_exprt{lhs, rhs};
      else if(id == ID_or)
        result = bitor_exprt{lhs, rhs};
      else if(id == ID_nand)
        result = bitnot_exprt{bitand_exprt{lhs, rhs}};
      else if(id == ID_nor)
        result = bitnot_exprt{bitor_exprt{lhs, rhs}};
      else if(id == ID_xor)
        result = bitxor_exprt{lhs, rhs};
      else
        result = bitnot_exprt{bitxor_exprt{lhs, rhs}};
      result.type() = type;
      return result;
    }
    else
      throw errort{}.with_location(expr.source_location())
        << "logical operator requires boolean, bit or vector operands";
  }
  else if(id == ID_equal || id == ID_notequal)
  {
    exprt result = id == ID_equal
                     ? static_cast<exprt>(equal_exprt{lhs, rhs})
                     : static_cast<exprt>(notequal_exprt{lhs, rhs});
    result.type() = vhdl_boolean_type();
    return result;
  }
  else if(id == ID_lt || id == ID_le || id == ID_gt || id == ID_ge)
  {
    if(
      !wider_type.has_value() && !is_vhdl_integer_type(type) &&
      !is_vhdl_vector_type(type) && type.id() != ID_enumeration &&
      type.id() != ID_bool)
      throw errort{}.with_location(expr.source_location())
        << "relational operator requires scalar or vector operands";

    exprt result = binary_relation_exprt{lhs, id, rhs};
    result.type() = vhdl_boolean_type();
    return result;
  }
  else if(
    id == ID_plus || id == ID_minus || id == ID_mult || id == ID_div ||
    id == ID_mod || id == ID_vhdl_rem || id == ID_power)
  {
    if(!is_vhdl_integer_type(type) && !is_vhdl_vector_type(type))
      throw errort{}.with_location(expr.source_location())
        << "arithmetic operator requires integer or vector operands";

    if(id == ID_power)
    {
      // only static
      auto base = typecheck_static_integer(lhs);
      auto exponent = typecheck_static_integer(rhs);
      if(exponent < 0)
        throw errort{}.with_location(expr.source_location())
          << "negative exponent";
      return from_integer(power(base, exponent), type);
    }

    // VHDL mod follows the sign of the right operand,
    // rem follows the sign of the left operand, as in C
    irep_idt cbmc_id = id == ID_vhdl_rem ? ID_mod : id;

    if(id == ID_mod && type.id() == ID_signedbv)
    {
      // a mod b = ((a rem b) + b) rem b
      exprt rem = binary_exprt{lhs, ID_mod, rhs, type};
      exprt result =
        binary_exprt{plus_exprt{std::move(rem), rhs}, ID_mod, rhs, type};
      return result;
    }

    return binary_exprt{std::move(lhs), cbmc_id, std::move(rhs), type};
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "unexpected binary operator: " << id;
}

exprt vhdl_typecheckt::typecheck_unary(exprt expr)
{
  auto id = expr.id();
  auto op = typecheck_expr(to_unary_expr(expr).op());

  if(id == ID_typecast)
  {
    // the VHDL-2008 condition operator ??
    if(is_vhdl_universal_type(op.type()))
      op = convert_to(std::move(op), vhdl_std_logic_type());
    if(!is_vhdl_scalar_bit_type(op.type()) && op.type().id() != ID_bool)
      throw errort{}.with_location(expr.source_location())
        << "?? requires a bit or boolean";
    op.type() = vhdl_boolean_type();
    return op;
  }

  if(is_vhdl_universal_type(op.type()))
  {
    if(op.type().id() == ID_natural)
      op = convert_to(std::move(op), vhdl_integer_type());
    else if(op.type().id() == ID_char)
      op = convert_to(std::move(op), vhdl_std_logic_type());
    else if(op.type().id() == ID_string)
    {
      vhdl_ranget range{
        mp_integer(op.get_string(ID_value).size()) - 1,
        0,
        vhdl_directiont::DOWNTO};
      op = convert_to(
        std::move(op), vhdl_unsigned_vector_type("std_logic_vector", range));
    }
    else
      throw errort{}.with_location(expr.source_location())
        << "cannot determine the type of the operand";
  }

  auto type = op.type();

  if(id == ID_not)
  {
    if(type.id() == ID_bool)
    {
      exprt result = not_exprt{std::move(op)};
      result.type() = type;
      return result;
    }
    else if(is_vhdl_vector_type(type))
      return bitnot_exprt{std::move(op)};
    else
      throw errort{}.with_location(expr.source_location())
        << "not requires a boolean, bit or vector operand";
  }
  else if(id == ID_unary_minus || id == ID_unary_plus || id == ID_abs)
  {
    if(!is_vhdl_integer_type(type) && !is_vhdl_vector_type(type))
      throw errort{}.with_location(expr.source_location())
        << "arithmetic operator requires an integer or vector operand";

    if(id == ID_unary_plus)
      return op;
    else if(id == ID_unary_minus)
      return unary_minus_exprt{std::move(op)};
    else
      return abs_exprt{std::move(op)};
  }
  else
    throw errort{}.with_location(expr.source_location())
      << "unexpected unary operator: " << id;
}
