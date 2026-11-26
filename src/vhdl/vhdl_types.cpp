/*******************************************************************\

Module: VHDL Types

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

/// \file
/// VHDL Types

#include "vhdl_types.h"

#include <util/arith_tools.h>
#include <util/invariant.h>

irep_idt to_irep_id(vhdl_directiont direction)
{
  return direction == vhdl_directiont::TO ? ID_to : ID_downto;
}

vhdl_directiont to_vhdl_direction(const irep_idt &id)
{
  PRECONDITION(id == ID_to || id == ID_downto);
  return id == ID_to ? vhdl_directiont::TO : vhdl_directiont::DOWNTO;
}

void vhdl_ranget::set(typet &type) const
{
  type.set(ID_C_vhdl_left, integer2string(left));
  type.set(ID_C_vhdl_right, integer2string(right));
  type.set(ID_C_vhdl_direction, to_irep_id(direction));
}

bool has_vhdl_range(const typet &type)
{
  return !type.get(ID_C_vhdl_direction).empty();
}

vhdl_ranget get_vhdl_range(const typet &type)
{
  PRECONDITION(has_vhdl_range(type));
  vhdl_ranget result;
  result.left = string2integer(type.get_string(ID_C_vhdl_left));
  result.right = string2integer(type.get_string(ID_C_vhdl_right));
  result.direction = to_vhdl_direction(type.get(ID_C_vhdl_direction));
  return result;
}

irep_idt vhdl_type_name(const typet &type)
{
  return type.get(ID_C_vhdl_type);
}

typet vhdl_boolean_type()
{
  typet result = bool_typet{};
  result.set(ID_C_vhdl_type, "boolean");
  return result;
}

static typet scalar_bit_type(const irep_idt &name)
{
  typet result = bool_typet{};
  result.set(ID_C_vhdl_type, name);
  return result;
}

typet vhdl_bit_type()
{
  return scalar_bit_type("bit");
}

typet vhdl_std_logic_type()
{
  return scalar_bit_type("std_logic");
}

typet vhdl_std_ulogic_type()
{
  return scalar_bit_type("std_ulogic");
}

typet vhdl_integer_type()
{
  typet result = signedbv_typet{32};
  result.set(ID_C_vhdl_type, "integer");
  return result;
}

typet vhdl_integer_type(const vhdl_ranget &range)
{
  auto result = vhdl_integer_type();
  range.set(result);
  return result;
}

typet vhdl_natural_type()
{
  auto result = vhdl_integer_type({0, power(2, 31) - 1, vhdl_directiont::TO});
  result.set(ID_C_vhdl_type, "natural");
  return result;
}

typet vhdl_positive_type()
{
  auto result = vhdl_integer_type({1, power(2, 31) - 1, vhdl_directiont::TO});
  result.set(ID_C_vhdl_type, "positive");
  return result;
}

typet vhdl_unsigned_vector_type(const irep_idt &name, const vhdl_ranget &range)
{
  typet result = unsignedbv_typet{numeric_cast_v<std::size_t>(range.length())};
  result.set(ID_C_vhdl_type, name);
  range.set(result);
  return result;
}

typet vhdl_signed_vector_type(const vhdl_ranget &range)
{
  typet result = signedbv_typet{numeric_cast_v<std::size_t>(range.length())};
  result.set(ID_C_vhdl_type, "signed");
  range.set(result);
  return result;
}

bool is_vhdl_scalar_bit_type(const typet &type)
{
  if(type.id() != ID_bool)
    return false;

  auto name = vhdl_type_name(type);
  return name == "bit" || name == "std_logic" || name == "std_ulogic";
}

typet vhdl_element_type(const typet &vector_type)
{
  PRECONDITION(is_vhdl_vector_type(vector_type));
  auto name = vhdl_type_name(vector_type);
  if(name == "bit_vector")
    return vhdl_bit_type();
  else if(name == "std_ulogic_vector")
    return vhdl_std_ulogic_type();
  else
    return vhdl_std_logic_type();
}

bool is_vhdl_integer_type(const typet &type)
{
  // integer, its subtypes, and user-defined integer types
  return type.id() == ID_signedbv && vhdl_type_name(type) != "signed" &&
         to_signedbv_type(type).get_width() == 32;
}

bool vhdl_types_compatible(const typet &a, const typet &b)
{
  if(a != b)
    return false;

  auto a_name = vhdl_type_name(a), b_name = vhdl_type_name(b);

  if(a_name.empty() || b_name.empty() || a_name == b_name)
    return true;

  // integer and its predefined subtypes
  auto is_predefined_integer = [](const irep_idt &name)
  { return name == "integer" || name == "natural" || name == "positive"; };

  if(is_predefined_integer(a_name) && is_predefined_integer(b_name))
    return true;

  // IEEE 1076-2008 makes std_logic a subtype of std_ulogic
  if(
    (a_name == "std_logic" || a_name == "std_ulogic") &&
    (b_name == "std_logic" || b_name == "std_ulogic"))
    return true;

  if(
    (a_name == "std_logic_vector" || a_name == "std_ulogic_vector") &&
    (b_name == "std_logic_vector" || b_name == "std_ulogic_vector"))
    return true;

  return false;
}

bool is_vhdl_vector_type(const typet &type)
{
  return (type.id() == ID_unsignedbv || type.id() == ID_signedbv) &&
         has_vhdl_range(type) && !is_vhdl_integer_type(type);
}

bool is_vhdl_universal_type(const typet &type)
{
  return type.id() == ID_natural || type.id() == ID_char ||
         type.id() == ID_string || type.id() == ID_vhdl_bit_string ||
         type.id() == ID_vhdl_aggregate || type.id() == ID_real;
}
