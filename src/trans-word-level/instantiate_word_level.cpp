/*******************************************************************\

Module: 

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "instantiate_word_level.h"

#include <util/ebmc_util.h>

#include <temporal-logic/temporal_expr.h>
#include <verilog/sva_expr.h>
#include <verilog/verilog_expr.h>

#include "property.h"

/*******************************************************************\

Function: timeframe_identifier

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::string
timeframe_identifier(const mp_integer &timeframe, const irep_idt &identifier)
{
  return id2string(identifier) + "@" + integer2string(timeframe);
}

/*******************************************************************\

Function: timeframe_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt timeframe_symbol(const mp_integer &timeframe, symbol_exprt src)
{
  auto result = std::move(src);
  result.set_identifier(
    timeframe_identifier(timeframe, result.get_identifier()));
  return result;
}

/*******************************************************************\

   Class: wl_instantiatet

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

class wl_instantiatet
{
public:
  wl_instantiatet(const mp_integer &_no_timeframes, const namespacet &_ns)
    : no_timeframes(_no_timeframes), ns(_ns)
  {
  }

  /// Instantiate the given expression for timeframe t
  [[nodiscard]] std::pair<mp_integer, exprt>
  operator()(exprt expr, const mp_integer &t) const
  {
    return instantiate_rec(std::move(expr), t);
  }

protected:
  const mp_integer &no_timeframes;
  const namespacet &ns;

  [[nodiscard]] std::pair<mp_integer, exprt>
  instantiate_rec(exprt, const mp_integer &t) const;
  [[nodiscard]] typet instantiate_rec(typet, const mp_integer &t) const;
};

/*******************************************************************\

Function: default_value

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static exprt default_value(const typet &type)
{
  auto zero = from_integer(0, type);
  if(zero.is_nil())
    throw "failed to create $past default value";
  else
    return std::move(zero);
}

/*******************************************************************\

Function: wl_instantiatet::instantiate_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::pair<mp_integer, exprt>
wl_instantiatet::instantiate_rec(exprt expr, const mp_integer &t) const
{
  expr.type() = instantiate_rec(expr.type(), t);

  if(expr.id() == ID_next_symbol)
  {
    expr.id(ID_symbol);
    auto u = t + 1;
    return {u, timeframe_symbol(u, to_symbol_expr(std::move(expr)))};
  }
  else if(expr.id() == ID_symbol)
  {
    return {t, timeframe_symbol(t, to_symbol_expr(std::move(expr)))};
  }
  else if(expr.id()==ID_sva_cycle_delay) // ##[1:2] something
  {
    auto &sva_cycle_delay_expr = to_sva_cycle_delay_expr(expr);

    if(sva_cycle_delay_expr.to().is_nil())
    {
      mp_integer offset;
      if(to_integer_non_constant(sva_cycle_delay_expr.from(), offset))
        throw "failed to convert sva_cycle_delay offset";

      const auto u = t + offset;

      // Do we exceed the bound? Make it 'true'
      if(u >= no_timeframes)
        return {no_timeframes - 1, true_exprt()};
      else
        return instantiate_rec(sva_cycle_delay_expr.op(), u);
    }
    else
    {
      mp_integer from, to;
      if(to_integer_non_constant(sva_cycle_delay_expr.from(), from))
        throw "failed to convert sva_cycle_delay offsets";

      if(sva_cycle_delay_expr.to().id() == ID_infinity)
      {
        DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
        to = no_timeframes - 1;
      }
      else if(to_integer_non_constant(sva_cycle_delay_expr.to(), to))
        throw "failed to convert sva_cycle_delay offsets";

      auto lower = t + from;
      auto upper = t + to;

      // Do we exceed the bound? Make it 'true'
      if(upper >= no_timeframes)
        return {no_timeframes - 1, true_exprt()};

      exprt::operandst disjuncts;

      for(mp_integer u = lower; u <= upper; ++u)
      {
        disjuncts.push_back(
          instantiate_rec(sva_cycle_delay_expr.op(), u).second);
      }

      return {upper, disjunction(disjuncts)};
    }
  }
  else if(expr.id()==ID_sva_sequence_concatenation)
  {
    // much like regular 'and'
    expr.id(ID_and);
    return instantiate_rec(expr, t);
  }
  else if(expr.id()==ID_sva_always)
  {
    auto &op = to_sva_always_expr(expr).op();

    exprt::operandst conjuncts;

    for(auto u = t; u < no_timeframes; ++u)
    {
      conjuncts.push_back(instantiate_rec(op, u).second);
    }

    return {no_timeframes - 1, conjunction(conjuncts)};
  }
  else if(expr.id() == ID_X)
  {
    const auto next = t + 1;

    if(next < no_timeframes)
    {
      return instantiate_rec(to_X_expr(expr).op(), next);
    }
    else
      return {no_timeframes - 1, true_exprt()}; // works on NNF only
  }
  else if(expr.id() == ID_sva_eventually)
  {
    const auto &eventually_expr = to_sva_eventually_expr(expr);
    const auto &op = eventually_expr.op();

    mp_integer lower;
    if(to_integer_non_constant(eventually_expr.lower(), lower))
      throw "failed to convert sva_eventually index";

    mp_integer upper;
    if(to_integer_non_constant(eventually_expr.upper(), upper))
      throw "failed to convert sva_eventually index";

    // This is weak, and passes if any of the timeframes
    // does not exist.
    if(t + lower >= no_timeframes || t + upper >= no_timeframes)
      return {no_timeframes - 1, true_exprt()};

    exprt::operandst disjuncts = {};

    for(mp_integer u = t + lower; u <= t + upper; ++u)
      disjuncts.push_back(instantiate_rec(op, u).second);

    return {no_timeframes - 1, disjunction(disjuncts)};
  }
  else if(
    expr.id() == ID_sva_s_eventually || expr.id() == ID_F || expr.id() == ID_AF)
  {
    const auto &p = to_unary_expr(expr).op();

    // The following needs to be satisfied for a counterexample
    // to Fp:
    // (1) There is a loop from the current state i back to
    //     some earlier state k < i.
    // (1) No state j with k<=k<=i on the lasso satisfies 'p'.
    //
    // We look backwards instead of forwards so that 't'
    // is the last state of the counterexample trace.
    //
    // Note that this is trivially true when t is zero,
    // as a single state cannot demonstrate the loop.

    exprt::operandst conjuncts = {};
    const auto i = t;

    for(mp_integer k = 0; k < i; ++k)
    {
      exprt::operandst disjuncts = {not_exprt(lasso_symbol(k, i))};

      for(mp_integer j = k; j <= i; ++j)
      {
        disjuncts.push_back(instantiate_rec(p, j).second);
      }

      conjuncts.push_back(disjunction(disjuncts));
    }

    return {no_timeframes - 1, conjunction(conjuncts)};
  }
  else if(expr.id()==ID_sva_until ||
          expr.id()==ID_sva_s_until)
  {
    // non-overlapping until
    // we need a lasso to refute these

    // we expand: p U q <=> q || (p && X(p U q))
    exprt tmp_q = to_binary_expr(expr).op1();
    tmp_q = instantiate_rec(tmp_q, t).second;

    exprt expansion = to_binary_expr(expr).op0();
    expansion = instantiate_rec(expansion, t).second;

    const auto next = t + 1;

    if(next < no_timeframes)
    {
      expansion = and_exprt(expansion, instantiate_rec(expr, next).second);
    }

    return {no_timeframes - 1, or_exprt(tmp_q, expansion)};
  }
  else if(expr.id()==ID_sva_until_with ||
          expr.id()==ID_sva_s_until_with)
  {
    // overlapping until
    
    // we rewrite using 'next'
    binary_exprt tmp = to_binary_expr(expr);
    if(expr.id()==ID_sva_until_with)
      tmp.id(ID_sva_until);
    else
      tmp.id(ID_sva_s_until);

    tmp.op1() = X_exprt(tmp.op1());

    return instantiate_rec(tmp, t);
  }
  else if(expr.id() == ID_verilog_past)
  {
    auto &verilog_past = to_verilog_past_expr(expr);

    mp_integer ticks;
    if(to_integer_non_constant(verilog_past.ticks(), ticks))
      throw "failed to convert $past number of ticks";

    if(ticks > t)
    {
      // return the 'default value' for the type
      return {t, default_value(verilog_past.type())};
    }
    else
    {
      return instantiate_rec(verilog_past.what(), t - ticks);
    }
  }
  else
  {
    mp_integer max = t;
    for(auto &op : expr.operands())
    {
      auto tmp = instantiate_rec(op, t);
      op = tmp.second;
      max = std::max(max, tmp.first);
    }

    return {max, expr};
  }
}

/*******************************************************************\

Function: wl_instantiatet::instantiate_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

typet wl_instantiatet::instantiate_rec(typet type, const mp_integer &) const
{
  return type;
}

/*******************************************************************\

Function: instantiate

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt instantiate(
  const exprt &expr,
  const mp_integer &t,
  const mp_integer &no_timeframes,
  const namespacet &ns)
{
  wl_instantiatet wl_instantiate(no_timeframes, ns);
  return wl_instantiate(expr, t).second;
}

/*******************************************************************\

Function: instantiate_property

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::pair<mp_integer, exprt> instantiate_property(
  const exprt &expr,
  const mp_integer &current,
  const mp_integer &no_timeframes,
  const namespacet &ns)
{
  wl_instantiatet wl_instantiate(no_timeframes, ns);
  return wl_instantiate(expr, current);
}
