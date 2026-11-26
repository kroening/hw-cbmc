/*******************************************************************\

Module: VHDL Types

Author: Daniel Kroening, dkr@amazon.com

\*******************************************************************/

/// \file
/// VHDL Types
///
/// VHDL types are mapped to CBMC types as follows. The original VHDL
/// type name is recorded in the comment #vhdl_type, and index ranges in
/// #vhdl_left, #vhdl_right and #vhdl_direction. Comments do not take
/// part in type equality.
///
/// boolean                 bool
/// bit, std_(u)logic       bool
/// integer and subtypes    signedbv(32), with the range as left/right
/// bit_vector, std_(u)logic_vector, unsigned
///                         unsignedbv(n), with the index range
/// signed                  signedbv(n), with the index range
/// enumeration types       enumeration, with the literals as elements
/// array types             array, with the index range
/// record types            struct

#ifndef CPROVER_VHDL_VHDL_TYPES_H
#define CPROVER_VHDL_VHDL_TYPES_H

#include <util/bitvector_types.h>
#include <util/mp_arith.h>
#include <util/std_types.h>

/// the direction of an index range or a slice
enum class vhdl_directiont
{
  TO,
  DOWNTO
};

irep_idt to_irep_id(vhdl_directiont);
vhdl_directiont to_vhdl_direction(const irep_idt &);

/// the index range of a vector or array type, or the range of
/// an integer subtype
struct vhdl_ranget
{
  mp_integer left, right;
  vhdl_directiont direction;

  mp_integer low() const
  {
    return direction == vhdl_directiont::TO ? left : right;
  }

  mp_integer high() const
  {
    return direction == vhdl_directiont::TO ? right : left;
  }

  /// the number of elements
  mp_integer length() const
  {
    auto l = high() - low() + 1;
    return l < 0 ? 0 : l;
  }

  void set(typet &) const;

  /// the position of the given VHDL index, counting from 0 for the
  /// element with the lowest index
  mp_integer position(const mp_integer &index) const
  {
    return index - low();
  }
};

/// true if the type carries an index range
bool has_vhdl_range(const typet &);
vhdl_ranget get_vhdl_range(const typet &);

/// the VHDL type name, or empty
irep_idt vhdl_type_name(const typet &);

typet vhdl_boolean_type();
typet vhdl_bit_type();
typet vhdl_std_logic_type();
typet vhdl_std_ulogic_type();
typet vhdl_integer_type();
typet vhdl_integer_type(const vhdl_ranget &);
typet vhdl_natural_type();
typet vhdl_positive_type();

/// bit_vector, std_logic_vector, std_ulogic_vector, unsigned
typet vhdl_unsigned_vector_type(const irep_idt &name, const vhdl_ranget &);
/// signed
typet vhdl_signed_vector_type(const vhdl_ranget &);

/// true for std_logic, std_ulogic and bit
bool is_vhdl_scalar_bit_type(const typet &);
/// the element type of a vector type
typet vhdl_element_type(const typet &vector_type);
/// true for the vector types
bool is_vhdl_vector_type(const typet &);
/// true for integer and its subtypes
bool is_vhdl_integer_type(const typet &);
/// true for the universal types of literals
bool is_vhdl_universal_type(const typet &);

/// true if the types are the same VHDL type, or compatible subtypes
bool vhdl_types_compatible(const typet &, const typet &);

#endif // CPROVER_VHDL_VHDL_TYPES_H
