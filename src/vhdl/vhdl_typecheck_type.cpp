/*******************************************************************\

Module: VHDL Type Checking of Types

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

/// \file
/// VHDL Type Checking of Types

#include "vhdl_typecheck_class.h"

#include <util/arith_tools.h>
#include <util/simplify_expr.h>

/// IEEE 1076-2008 9.4.2: a static expression over literals and constants
mp_integer vhdl_typecheckt::typecheck_static_integer(const exprt &expr)
{
  auto typed = typecheck_expr(expr);

  if(is_vhdl_universal_type(typed.type()))
    typed = convert_to(typed, vhdl_integer_type());

  auto simplified = simplify_expr(typed, ns);

  if(!simplified.is_constant())
    throw errort{}.with_location(expr.source_location())
      << "expected a static expression";

  auto value = numeric_cast<mp_integer>(to_constant_expr(simplified));

  if(!value.has_value())
    throw errort{}.with_location(expr.source_location())
      << "expected a static integer expression";

  return *value;
}

/// A range, given as two bounds with a direction, or as a range
/// attribute, e.g., v'range.
vhdl_ranget vhdl_typecheckt::typecheck_range(const exprt &range)
{
  if(range.id() == ID_range)
  {
    auto &binary = to_binary_expr(range);
    vhdl_ranget result;
    result.left = typecheck_static_integer(binary.op0());
    result.right = typecheck_static_integer(binary.op1());
    result.direction = to_vhdl_direction(range.get(ID_direction));
    return result;
  }
  else if(
    range.id() == ID_vhdl_attribute &&
    (to_lower(range.get(ID_identifier)) == "range" ||
     to_lower(range.get(ID_identifier)) == "reverse_range"))
  {
    auto &prefix = to_unary_expr(range).op();

    // the prefix may be a type or an object
    typet type;

    const typet *type_ptr = prefix.id() == ID_symbol
                              ? lookup_type(to_lower(prefix.get(ID_identifier)))
                              : nullptr;

    if(type_ptr != nullptr)
      type = *type_ptr;
    else
      type = typecheck_expr(prefix).type();

    if(!has_vhdl_range(type))
      throw errort{}.with_location(range.source_location())
        << "range attribute requires an array type";

    auto result = get_vhdl_range(type);

    if(to_lower(range.get(ID_identifier)) == "reverse_range")
    {
      std::swap(result.left, result.right);
      result.direction = result.direction == vhdl_directiont::TO
                           ? vhdl_directiont::DOWNTO
                           : vhdl_directiont::TO;
    }

    return result;
  }
  else if(range.id() == ID_vhdl_subtype_indication)
  {
    // e.g., integer range 0 to 7
    auto type = typecheck_subtype_indication(
      static_cast<const typet &>(static_cast<const irept &>(range)));
    if(!has_vhdl_range(type))
      throw errort{}.with_location(range.source_location())
        << "expected a range";
    return get_vhdl_range(type);
  }
  else
    throw errort{}.with_location(range.source_location()) << "expected a range";
}

/// The name of a type, possibly a selected name such as
/// ieee.std_logic_1164.std_logic; only the last component is used.
typet vhdl_typecheckt::typecheck_type_mark(const exprt &name)
{
  irep_idt base_name;

  if(name.id() == ID_symbol)
    base_name = name.get(ID_identifier);
  else if(name.id() == ID_member)
    base_name = name.get(ID_component_name);
  else
    throw errort{}.with_location(name.source_location())
      << "expected a type name";

  auto type_ptr = lookup_type(to_lower(base_name));

  if(type_ptr == nullptr)
    throw errort{}.with_location(name.source_location())
      << "unknown type `" << base_name << "'";

  return *type_ptr;
}

/// IEEE 1076-2008 6.3: a type mark with an optional constraint
typet vhdl_typecheckt::typecheck_subtype_indication(const typet &type)
{
  if(type.id() != ID_vhdl_subtype_indication)
    return typecheck_type(type);

  auto &name = static_cast<const exprt &>(type.find(ID_name));
  auto base = typecheck_type_mark(name);

  auto &range_constraint =
    static_cast<const exprt &>(type.find(ID_range_constraint));
  auto &index_constraint = type.find(ID_index_constraint);

  if(range_constraint.is_not_nil())
  {
    // e.g., integer range 0 to 7
    if(!is_vhdl_integer_type(base))
      throw errort{}.with_location(type.source_location())
        << "range constraint requires a scalar type";

    auto range = typecheck_range(range_constraint);
    typet result = base;
    range.set(result);
    return result;
  }
  else if(index_constraint.is_not_nil())
  {
    // e.g., std_logic_vector(7 downto 0)
    if(index_constraint.get_sub().size() != 1)
      throw errort{}.with_location(type.source_location())
        << "only one-dimensional arrays are supported";

    auto range = typecheck_range(
      static_cast<const exprt &>(index_constraint.get_sub().front()));

    if(base.id() == ID_unconstrained)
    {
      auto type_name = vhdl_type_name(base);
      if(type_name == "signed")
        return vhdl_signed_vector_type(range);
      else
        return vhdl_unsigned_vector_type(type_name, range);
    }
    else if(base.id() == ID_array && !has_vhdl_range(base))
    {
      // an unconstrained user-defined array type
      auto result = to_array_type(base);
      result.size() = from_integer(range.length(), vhdl_integer_type());
      range.set(result);
      return std::move(result);
    }
    else
      throw errort{}.with_location(type.source_location())
        << "index constraint requires an unconstrained array type";
  }
  else
  {
    if(base.id() == ID_unconstrained)
      throw errort{}.with_location(type.source_location())
        << "type `" << vhdl_type_name(base) << "' requires a constraint";

    return base;
  }
}

/// IEEE 1076-2008 6.2: a type definition
typet vhdl_typecheckt::typecheck_type(const typet &type)
{
  if(type.id() == ID_vhdl_subtype_indication)
  {
    return typecheck_subtype_indication(type);
  }
  else if(type.id() == ID_enumeration)
  {
    // the literals are lower case
    enumeration_typet result;
    result.add_source_location() = type.source_location();

    for(auto &element : type.find(ID_elements).get_sub())
    {
      auto &element_expr = static_cast<const exprt &>(element);
      irep_idt literal;

      if(element_expr.id() == ID_symbol)
        literal = to_lower(element_expr.get(ID_identifier));
      else if(element_expr.id() == ID_constant)
        literal = element_expr.get(ID_value); // character literal
      else
        throw errort{}.with_location(element_expr.source_location())
          << "unexpected enumeration literal";

      result.elements().push_back(irept{literal});
    }

    return std::move(result);
  }
  else if(type.id() == ID_range)
  {
    // an integer type
    auto range =
      typecheck_range(static_cast<const exprt &>(type.find(ID_range)));
    return vhdl_integer_type(range);
  }
  else if(type.id() == ID_array)
  {
    auto element_type = typecheck_subtype_indication(
      static_cast<const typet &>(type.find(ID_element_type)));

    auto &index_constraint = type.find(ID_index_constraint);

    if(index_constraint.get_sub().size() != 1)
      throw errort{}.with_location(type.source_location())
        << "only one-dimensional arrays are supported";

    auto &index =
      static_cast<const exprt &>(index_constraint.get_sub().front());

    if(index.id() == ID_unconstrained)
    {
      // e.g., array (natural range <>) of ...
      array_typet result{element_type, nil_exprt{}};
      result.add_source_location() = type.source_location();
      return std::move(result);
    }
    else
    {
      auto range = typecheck_range(index);
      array_typet result{
        element_type, from_integer(range.length(), vhdl_integer_type())};
      range.set(result);
      result.add_source_location() = type.source_location();
      return std::move(result);
    }
  }
  else if(type.id() == ID_struct)
  {
    struct_typet result;
    result.add_source_location() = type.source_location();

    for(auto &element : type.find(ID_elements).get_sub())
    {
      auto element_type = typecheck_subtype_indication(
        static_cast<const typet &>(element.find(ID_type)));

      for(auto &declarator : element.find(ID_declarators).get_sub())
      {
        irep_idt name = to_lower(declarator.get(ID_identifier));
        for(auto &c : result.components())
          if(c.get_name() == name)
            throw errort{}.with_location(type.source_location())
              << "record element `" << name << "' is already declared";
        result.components().emplace_back(name, element_type);
        result.components().back().set_pretty_name(name);
      }
    }

    return std::move(result);
  }
  else
    throw errort{}.with_location(type.source_location())
      << "unexpected type: " << type.id();
}

/// IEEE 1076-2008 6.4.2.3: the default initial value of an object
/// is T'left. For std_logic, this would be 'U', which we do not
/// model; the value is left undetermined.
exprt vhdl_typecheckt::default_value(const typet &type)
{
  auto type_name = vhdl_type_name(type);

  if(type_name == "boolean")
    return false_exprt{};
  else if(type_name == "bit")
  {
    exprt result = false_exprt{};
    result.type() = type;
    return result;
  }
  else if(type_name == "std_logic" || type_name == "std_ulogic")
    return nil_exprt{}; // 'U'
  else if(type.id() == ID_bool)
    return false_exprt{};
  else if(type_name == "bit_vector")
    return from_integer(0, type);
  else if(is_vhdl_integer_type(type))
  {
    if(has_vhdl_range(type))
      return from_integer(get_vhdl_range(type).left, type);
    else
      return from_integer(-power(2, 31), type); // integer'left
  }
  else if(type.id() == ID_enumeration)
  {
    auto &elements = to_enumeration_type(type).elements();
    DATA_INVARIANT(!elements.empty(), "enumeration types have literals");
    return constant_exprt{elements.front().id(), type};
  }
  else if(type.id() == ID_array)
  {
    auto element_default = default_value(to_array_type(type).element_type());
    if(element_default.is_nil())
      return nil_exprt{};
    return array_of_exprt{element_default, to_array_type(type)};
  }
  else if(type.id() == ID_struct)
  {
    struct_exprt result{{}, type};
    for(auto &c : to_struct_type(type).components())
    {
      auto element_default = default_value(c.type());
      if(element_default.is_nil())
        return nil_exprt{};
      result.operands().push_back(std::move(element_default));
    }
    return std::move(result);
  }
  else
    return nil_exprt{};
}
