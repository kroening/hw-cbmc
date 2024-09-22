/*******************************************************************\

Module: 

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "instantiate_word_level.h"

#include <util/ebmc_util.h>
#include <util/expr_util.h>

#include <ebmc/ebmc_error.h>
#include <temporal-logic/temporal_expr.h>
#include <temporal-logic/temporal_logic.h>
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

  // Returns a list of match points and matching conditions
  // for the given sequence expression starting at time t
  [[nodiscard]] std::vector<std::pair<mp_integer, exprt>>
  instantiate_sequence(exprt, const mp_integer &t) const;
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

Function: wl_instantiatet::instantiate_sequence

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::vector<std::pair<mp_integer, exprt>>
wl_instantiatet::instantiate_sequence(exprt expr, const mp_integer &t) const
{
  if(expr.id() == ID_sva_cycle_delay) // ##[1:2] something
  {
    auto &sva_cycle_delay_expr = to_sva_cycle_delay_expr(expr);

    if(sva_cycle_delay_expr.to().is_nil()) // ##1 something
    {
      mp_integer offset;
      if(to_integer_non_constant(sva_cycle_delay_expr.from(), offset))
        throw "failed to convert sva_cycle_delay offset";

      const auto u = t + offset;

      // Do we exceed the bound? Make it 'true'
      if(u >= no_timeframes)
      {
        DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
        return {{no_timeframes - 1, true_exprt()}};
      }
      else
        return instantiate_sequence(sva_cycle_delay_expr.op(), u);
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
      {
        DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
        return {{no_timeframes - 1, true_exprt()}};
      }

      std::vector<std::pair<mp_integer, exprt>> match_points;

      for(mp_integer u = lower; u <= upper; ++u)
      {
        auto sub_result = instantiate_sequence(sva_cycle_delay_expr.op(), u);
        for(auto &match_point : sub_result)
          match_points.push_back(match_point);
      }

      return match_points;
    }
  }
  else if(
    expr.id() == ID_sva_sequence_concatenation ||
    expr.id() == ID_sva_overlapped_implication ||
    expr.id() == ID_sva_non_overlapped_implication)
  {
    auto &implication = to_binary_expr(expr);
    std::vector<std::pair<mp_integer, exprt>> result;

    // This is the product of the match points on the LHS and RHS
    const auto lhs_match_points = instantiate_sequence(implication.lhs(), t);
    for(auto &lhs_match_point : lhs_match_points)
    {
      // The RHS of the non-overlapped implication starts one timeframe later
      auto t_rhs = expr.id() == ID_sva_non_overlapped_implication
                     ? lhs_match_point.first + 1
                     : lhs_match_point.first;

      // Do we exceed the bound? Make it 'true'
      if(t_rhs >= no_timeframes)
      {
        DATA_INVARIANT(no_timeframes != 0, "must have timeframe");
        return {{no_timeframes - 1, true_exprt()}};
      }

      const auto rhs_match_points =
        instantiate_sequence(implication.rhs(), t_rhs);

      for(auto &rhs_match_point : rhs_match_points)
      {
        exprt cond;
        if(expr.id() == ID_sva_sequence_concatenation)
        {
          cond = and_exprt{lhs_match_point.second, rhs_match_point.second};
        }
        else if(
          expr.id() == ID_sva_overlapped_implication ||
          expr.id() == ID_sva_non_overlapped_implication)
        {
          cond = implies_exprt{lhs_match_point.second, rhs_match_point.second};
        }
        else
          PRECONDITION(false);

        result.push_back({rhs_match_point.first, cond});
      }
    }

    return result;
  }
  else if(expr.id() == ID_sva_sequence_intersect)
  {
    // IEEE 1800-2017 16.9.6
    PRECONDITION(false);
  }
  else if(expr.id() == ID_sva_sequence_first_match)
  {
    PRECONDITION(false);
  }
  else if(expr.id() == ID_sva_sequence_throughout)
  {
    PRECONDITION(false);
  }
  else if(expr.id() == ID_sva_sequence_within)
  {
    PRECONDITION(false);
  }
  else if(expr.id() == ID_sva_and)
  {
    // IEEE 1800-2017 16.9.5
    // 1. Both operands must match.
    // 2. Both sequences start at the same time.
    // 3. The end time of the composite sequence is
    //    the end time of the operand sequence that completes last.
    // Condition (3) is TBD.
    exprt::operandst conjuncts;

    for(auto &op : expr.operands())
      conjuncts.push_back(instantiate_rec(op, t).second);

    exprt condition = conjunction(conjuncts);
    return {{t, condition}};
  }
  else if(expr.id() == ID_sva_or)
  {
    // IEEE 1800-2017 16.9.7
    // The set of matches of a or b is the set union of the matches of a
    // and the matches of b.
    std::vector<std::pair<mp_integer, exprt>> result;

    for(auto &op : expr.operands())
      for(auto &match_point : instantiate_sequence(op, t))
        result.push_back(match_point);

    return result;
  }
  else
  {
    // not a sequence, evaluate as state predicate
    return {instantiate_rec(expr, t)};
  }
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
  else if(is_SVA_sequence(expr))
  {
    // sequence expressions -- these may have multiple potential
    // match points, and evaluate to true if any of them matches
    const auto match_points = instantiate_sequence(expr, t);
    exprt::operandst disjuncts;
    disjuncts.reserve(match_points.size());
    mp_integer max = t;

    for(auto &match_point : match_points)
    {
      disjuncts.push_back(match_point.second);
      max = std::max(max, match_point.first);
    }

    return {max, disjunction(disjuncts)};
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
  else if(is_temporal_operator(expr))
  {
    // should have been done by property_obligations_rec
    throw ebmc_errort() << "instantiate_word_level got temporal operator "
                        << expr.id();
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
