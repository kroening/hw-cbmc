/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cstdlib>
#include <sstream>

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/lispexpr.h>
#include <util/lispirep.h>
#include <util/std_expr.h>

#include "expr2vhdl.h"
#include "expr2vhdl_class.h"

/*******************************************************************\

Function: expr2vhdlt::convert_trinary

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_trinary(
  const ternary_exprt &src,
  const std::string &symbol1,
  const std::string &symbol2,
  unsigned precedence)
{
  std::string dest;
  unsigned p0, p1, p2;

  std::string op0=convert(src.op0(), p0);
  std::string op1=convert(src.op1(), p1);
  std::string op2=convert(src.op2(), p2);

  if(precedence>p0) dest+='(';
  dest+=op0;
  if(precedence>p0) dest+=')';

  dest+=' ';
  dest+=symbol1;
  dest+=' ';

  if(precedence>p1) dest+='(';
  dest+=op1;
  if(precedence>p1) dest+=')';

  dest+=' ';
  dest+=symbol2;
  dest+=' ';

  if(precedence>p2) dest+='(';
  dest+=op2;
  if(precedence>p2) dest+=')';

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_binary

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_binary(
  const exprt &src,
  const std::string &symbol,
  unsigned precedence)
{
  if(src.operands().size()<2)
    return convert_norep(src, precedence);

  bool first=true;
  std::string dest;

  forall_operands(it, src)
  {
    if(first)
      first=false;
    else
    {
      dest+=' ';
      dest+=symbol;
      dest+=' ';
    }

    unsigned p;
    std::string op=convert(*it, p);

    if(precedence>p) dest+='(';
    dest+=op;
    if(precedence>p) dest+=')';
  }

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_with

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_with(const with_exprt &src, unsigned precedence)
{
  if(src.operands().size()<1)
    return convert_norep(src, precedence);

  unsigned p;
  std::string dest = "(" + convert(src.old(), p);

  for(unsigned i=1; i<src.operands().size(); i+=2)
  {
    dest+=" WITH ";
    dest+=convert(src.operands()[i], p);
    dest+=":=";
    dest+=convert(src.operands()[i+1], p);
  }

  dest+=")";

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_concatenation

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_concatenation(
  const concatenation_exprt &src,
  unsigned precedence)
{
  bool first=true;
  std::string dest="{ ";

  forall_operands(it, src)
  {
    if(first)
      first=false;
    else
      dest+=", ";

    unsigned p;
    std::string op=convert(*it, p);

    if(precedence>p) dest+='(';
    dest+=op;
    if(precedence>p) dest+=')';
  }

  dest+=" }";

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_replication

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_replication(
  const replication_exprt &src,
  unsigned precedence)
{
  std::string dest="{ ";

  dest+=convert(src.op0());
  dest+=" { ";
  dest+=convert(src.op1());
  dest+=" } }";

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_unary

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_unary(
  const unary_exprt &src,
  const std::string &symbol,
  unsigned precedence)
{
  unsigned p;
  std::string op = convert(src.op(), p);

  std::string dest=symbol;
  if(precedence>p) dest+='(';
  dest+=op;
  if(precedence>p) dest+=')';

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_typecast

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_typecast(const typecast_exprt &src, unsigned &precedence)
{
  //const typet &from=src.op0().type();
  //const typet &to=src.type();

  // just ignore them for now
  return convert(src.op(), precedence);
}

/*******************************************************************\

Function: expr2vhdlt::convert_index

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_index(const index_exprt &src, unsigned precedence)
{
  unsigned p;
  std::string op=convert(src.op0(), p);

  std::string dest;
  if(precedence>p) dest+='(';
  dest+=op;
  if(precedence>p) dest+=')';

  dest+='[';
  dest+=convert(src.op1());
  dest+=']';

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_extractbit

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_extractbit(const extractbit_exprt &src, unsigned precedence)
{
  unsigned p;
  std::string op=convert(src.op0(), p);

  std::string dest;
  if(precedence>p) dest+='(';
  dest+=op;
  if(precedence>p) dest+=')';

  dest+='[';
  dest+=convert(src.op1());
  dest+=']';

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_extractbits

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_extractbits(
  const extractbits_exprt &src,
  unsigned precedence)
{
  unsigned p;
  std::string op = convert(src.src(), p);

  std::string dest;
  if(precedence>p) dest+='(';
  dest+=op;
  if(precedence>p) dest+=')';

  auto width = to_bitvector_type(src.type()).get_width();

  dest+='[';

  if(src.index().is_constant())
  {
    auto index_int = numeric_cast_v<mp_integer>(to_constant_expr(src.index()));
    dest += integer2string(index_int + width);
  }
  else
  {
    dest += convert(src.index());
    dest += " + ";
    dest += std::to_string(width);
  }

  dest+=':';
  dest += convert(src.index());
  dest+=']';

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_member

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_member(const member_exprt &src, unsigned precedence)
{
  unsigned p;
  std::string op = convert(src.compound(), p);

  std::string dest;
  if(precedence>p) dest+='(';
  dest+=op;
  if(precedence>p) dest+=')';

  dest+='.';
  dest+=src.get_string(ID_component_name);

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_norep

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_norep(
  const exprt &src,
  unsigned &precedence)
{
  precedence=22;
  return src.pretty();
}

/*******************************************************************\

Function: expr2vhdlt::convert_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_symbol(const symbol_exprt &src, unsigned &precedence)
{
  precedence=22;
  std::string dest = id2string(src.get_identifier());

  if(std::string(dest, 0, 9)=="Verilog::")
    dest.erase(0, 9);

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_nondet_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_nondet_symbol(
  const exprt &src,
  unsigned &precedence)
{
  return "nondet(" +
         convert_symbol(
           symbol_exprt(src.get(ID_identifier), src.type()), precedence) +
         ")";
}

/*******************************************************************\

Function: expr2vhdlt::convert_next_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert_next_symbol(
  const exprt &src,
  unsigned &precedence)
{
  return "next(" +
         convert_symbol(
           symbol_exprt(src.get(ID_identifier), src.type()), precedence) +
         ")";
}

/*******************************************************************\

Function: expr2vhdlt::convert_constant

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_constant(const constant_exprt &src, unsigned &precedence)
{
  precedence=22;

  const typet &type=src.type();
  const std::string &value = id2string(src.get_value());
  std::string dest;

  if(type.id()==ID_bool)
  {
    if(src.is_true())
      dest+="1";
    else
      dest+="0";
  }
  else if(type.id()==ID_unsignedbv ||
          type.id()==ID_signedbv)
  {
    auto i = numeric_cast_v<mp_integer>(src);

    if(i>=256)
      dest="'h"+integer2string(i, 16);
    else
      dest=integer2string(i);
  }
  else if(type.id()==ID_integer || type.id()==ID_natural)
    dest=value;
  else if(type.id()==ID_char)
  {
    dest='\''+value+'\'';
  }
  else
    return convert_norep(src, precedence);

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert_array

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
expr2vhdlt::convert_array(const array_exprt &src, unsigned precedence)
{
  std::string dest="{ ";

  forall_operands(it, src)
  {
    std::string tmp=convert(*it, precedence);
      
    exprt::operandst::const_iterator next_it=it;
    next_it++;

    if(next_it!=src.operands().end())
    {
      tmp+=", ";
      if(tmp.size()>40) tmp+="\n    ";
    }

    dest+=tmp;
  }

  dest+=" }";

  return dest;
}

/*******************************************************************\

Function: expr2vhdlt::convert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert(
  const exprt &src,
  unsigned &precedence)
{
  precedence=22;

  if(src.id()=="+")
    return convert_binary(src, "+", precedence=14);

  else if(src.id()==ID_if)
    return convert_trinary(to_if_expr(src), "?", ":", precedence = 14);

  else if(src.id()==ID_concatenation)
    return convert_concatenation(to_concatenation_expr(src), precedence = 16);

  else if(src.id()==ID_with)
    return convert_with(to_with_expr(src), precedence = 16);

  else if(src.id()==ID_replication)
    return convert_replication(to_replication_expr(src), precedence = 22);

  else if(src.id()==ID_array)
    return convert_array(to_array_expr(src), precedence = 22);

  else if(src.id()=="-")
  {
    if(src.operands().size()<2)
      return convert_norep(src, precedence);
    else     
      return convert_binary(src, "-", precedence=14);
  }

  else if(src.id()==ID_shl)
    return convert_binary(src, "<<", precedence=14);

  else if(src.id()==ID_lshr)
    return convert_binary(src, ">>", precedence=14);

  else if(src.id() == ID_unary_minus)
  {
    if(src.operands().size()!=1)
      return convert_norep(src, precedence);
    else
      return convert_unary(to_unary_minus_expr(src), "-", precedence = 16);
  }

  else if(src.id()==ID_index)
    return convert_index(to_index_expr(src), precedence = 22);

  else if(src.id()==ID_extractbit)
    return convert_extractbit(to_extractbit_expr(src), precedence = 22);

  else if(src.id()==ID_extractbits)
    return convert_extractbits(to_extractbits_expr(src), precedence = 22);

  else if(src.id()==ID_member)
    return convert_member(to_member_expr(src), precedence = 22);

  else if(src.id()=="*" || src.id()=="/")
    return convert_binary(src, src.id_string(), precedence=14);

  else if(src.id()=="<" || src.id()==">" ||
          src.id()=="<=" || src.id()==">=")
    return convert_binary(src, src.id_string(), precedence=9);

  else if(src.id()=="=")
    return convert_binary(src, "==", precedence=9);

  else if(src.id()==ID_notequal)
    return convert_binary(to_notequal_expr(src), "!=", precedence = 9);

  else if(src.id()==ID_not)
    return convert_unary(to_not_expr(src), "!", precedence = 16);

  else if(src.id()==ID_bitnot)
    return convert_unary(to_bitnot_expr(src), "~", precedence = 16);

  else if(src.id()==ID_typecast)
    return convert_typecast(to_typecast_expr(src), precedence);

  else if(src.id()==ID_and)
    return convert_binary(src, "&&", precedence=7);

  else if(src.id()==ID_bitand)
    return convert_binary(src, "&", precedence=7);

  else if(src.id()==ID_or)
    return convert_binary(src, "||", precedence=6);

  else if(src.id()==ID_bitor)
    return convert_binary(src, "|", precedence=6);

  else if(src.id()=="=>")
    return convert_binary(src, "->", precedence=5);

  else if(src.id()=="<=>")
    return convert_binary(src, "<->", precedence=4);

  else if(src.id()==ID_AG || src.id()==ID_EG ||
          src.id()==ID_AX || src.id()==ID_EX)
    return convert_unary(
      to_unary_expr(src), src.id_string() + " ", precedence = 4);

  else if(src.id()==ID_symbol)
    return convert_symbol(to_symbol_expr(src), precedence);

  else if(src.id()==ID_nondet_symbol)
    return convert_nondet_symbol(src, precedence);

  else if(src.id()==ID_next_symbol)
    return convert_next_symbol(src, precedence);

  else if(src.id()==ID_constant)
    return convert_constant(to_constant_expr(src), precedence);

  else if(src.id()=="vhdl-constant")
  {
    const std::string width=src.type().get_string(ID_width);
    return width+"'b"+src.get_string(ID_value);
  }

  // no VERILOG language expression for internal representation 
  return convert_norep(src, precedence);
}

/*******************************************************************\

Function: expr2vhdlt::convert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert(const exprt &src)
{
  unsigned precedence;
  return convert(src, precedence);
}

/*******************************************************************\

Function: expr2vhdlt::convert

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdlt::convert(const typet &type)
{
  if(type.id()==ID_bool)
    return "boolean";
  else if(type.id()==ID_unsignedbv || type.id()==ID_signedbv)
  {
    unsigned width=to_bitvector_type(type).get_width();
    bool little_endian = type.get_bool(ID_C_big_endian);
    unsigned offset=atoi(type.get(ID_C_offset).c_str());

    if(width!=0)
    {
      std::string dest;
      if(type.id()==ID_unsignedbv)
        dest="bv";
      else if(type.id()==ID_signedbv)
        dest="signed bv";

      dest+='[';
      
      if(little_endian)
      {
        dest+=std::to_string(offset+width-1);
        dest+=":";
        dest+=std::to_string(offset);
      }
      else
      {
        dest+=std::to_string(offset);
        dest+=":";
        dest+=std::to_string(offset+width-1);
      }

      dest+="]";
      return dest;
    }
  }
  else if(type.id()==ID_array)
  {
    auto &array_type = to_array_type(type);
    std::string dest="array [";

    dest += convert(array_type.size());

    dest+="] of ";
    dest += convert(array_type.element_type());

    return dest;
  }
  else if(type.id()==ID_integer)
    return "integer";
  else if(type.id()==ID_real)
    return "real";

  return "IREP("+type.pretty()+")";
}

/*******************************************************************\

Function: expr2vhdl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string expr2vhdl(const exprt &expr)
{
  expr2vhdlt expr2vhdl;
  return expr2vhdl.convert(expr);
}

/*******************************************************************\

Function: type2vhdl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string type2vhdl(const typet &type)
{
  expr2vhdlt expr2vhdl;
  return expr2vhdl.convert(type);
}
