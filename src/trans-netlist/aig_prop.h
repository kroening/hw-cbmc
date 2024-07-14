/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_TRANS_NETLIST_AIG_PROP_H
#define CPROVER_TRANS_NETLIST_AIG_PROP_H

#include <util/invariant.h>
#include <util/threeval.h>

#include <solvers/prop/prop.h>

#include "aig.h"

class aig_prop_baset : public propt {
public:
  explicit aig_prop_baset(aigt &_dest, message_handlert &message_handler)
      : propt(message_handler), dest(_dest) {}

  bool has_set_to() const override { return false; }
  bool cnf_handled_well() const override { return false; }

  literalt land(literalt a, literalt b) override;
  literalt lor(literalt a, literalt b) override;
  literalt land(const bvt &bv) override;
  literalt lor(const bvt &bv) override;
  void lcnf(const bvt &clause) override
  {
    PRECONDITION(false);
  }
  literalt lxor(literalt a, literalt b) override;
  literalt lxor(const bvt &bv) override;
  literalt lnand(literalt a, literalt b) override;
  literalt lnor(literalt a, literalt b) override;
  literalt lequal(literalt a, literalt b) override;
  literalt limplies(literalt a, literalt b) override;
  literalt lselect(literalt a, literalt b, literalt c) override; // a?b:c
  void set_equal(literalt a, literalt b) override;

  void l_set_to(literalt a, bool value) override
  {
    PRECONDITION(false);
  }

  literalt new_variable() override { return dest.new_node(); }

  size_t no_variables() const override { return dest.number_of_nodes(); }

  std::string solver_text() const override {
    return "conversion into and-inverter graph";
  }

  tvt l_get(literalt a) const override {
    PRECONDITION(false);
    return tvt::unknown();
  }

  resultt prop_solve() {
    PRECONDITION(false);
    return resultt::P_ERROR;
  }

protected:
  aigt &dest;
};

class aig_prop_constraintt : public aig_prop_baset {
public:
  explicit aig_prop_constraintt(aig_plus_constraintst &_dest,
                                message_handlert &message_handler)
      : aig_prop_baset(_dest, message_handler), dest(_dest) {}

  aig_plus_constraintst &dest;
  bool has_set_to() const override { return true; }

  void lcnf(const bvt &clause) override { l_set_to_true(lor(clause)); }

  void l_set_to(literalt a, bool value) override {
    dest.constraints.push_back(a ^ !value);
  }

  void set_assignment(literalt a, bool value) override {}
  bool is_in_conflict(literalt l) const override { return false; }
  resultt do_prop_solve(const bvt &assumptions) override
  {
    return resultt{};
  }
};

#endif // CPROVER_TRANS_NETLIST_AIG_PROP_H
