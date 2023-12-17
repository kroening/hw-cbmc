/*******************************************************************\

Module: CEGAR for BMC

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "bmc_cegar.h"

#include <trans-netlist/instantiate_netlist.h>
#include <trans-netlist/unwind_netlist.h>
#include <trans-netlist/ldg.h>
#include <trans-netlist/trans_to_netlist.h>
#include <trans-netlist/compute_ct.h>

#include <cassert>
#include <chrono>

/*******************************************************************\

Function: bmc_cegart::bmc_cegar

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmc_cegart::bmc_cegar()
{
  if(properties.properties.empty())
  {
    error() << "No properties given" << eom;
    return;
  }

  auto start_time=std::chrono::steady_clock::now();

  try { cegar_loop(); }
  
  catch(int)
  {
  }

  auto stop_time = std::chrono::steady_clock::now();

  statistics()
    << "CEGAR time: "
    << std::chrono::duration<double>(stop_time-start_time).count()
    << eom;
}

/*******************************************************************\

Function: bmc_cegart::unwind

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmc_cegart::unwind(
  unsigned bound,
  const netlistt &netlist,
  propt &prop)
{
  // allocate timeframes
  bmc_mapt bmc_map;
  bmc_map.map_timeframes(netlist, bound+1, prop);

  #if 0
  for(unsigned timeframe=0; timeframe<=bound; timeframe++)
    bmc_map.timeframe_map[timeframe].resize(aig_map.no_vars);

  // do initial state
  for(unsigned v=0; v<aig_map.no_vars; v++)
    bmc_map.timeframe_map[0][v]=prop.new_variable();

  // do transitions
  for(unsigned timeframe=0; timeframe<bound; timeframe++)
  {
    status() << "Round " << timeframe << eom;
    
    aig.clear_convert_cache();
    
    // set current state bits
    for(unsigned v=0; v<aig_map.no_vars; v++)
    {
      //std::cout << "SETTING "
      //          << aig_map.timeframe_map[0][v] << std::endl;

      aig.set_l(prop,
                      aig_map.timeframe_map[0][v],
                      bmc_map.timeframe_map[timeframe][v]);
    }

    // convert next state bits
    for(unsigned v=0; v<aig_map.no_vars; v++)
    {
      literalt a=aig_map.timeframe_map[1][v];
    
      // std::cout << "CONVERTING " << a << std::endl;

      literalt l;

      if(latches.find(v)!=latches.end())
      {
        assert(aig.can_convert(a));

        l=aig.convert_prop(prop, a);
      }
      else
        l=prop.new_variable();
      
      bmc_map.timeframe_map[timeframe+1][v]=l;
    }
  }

  instantiate(prop, bmc_map, initial_state_predicate, 0, 1,
              false, ns);
  
  // do the property
  property(properties, prop_bv, get_message_handler(), prop,
           bmc_map, ns);
  #endif
}

/*******************************************************************\

Function: bmc_cegart::compute_ct

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

unsigned bmc_cegart::compute_ct()
{
  status() << "Computing CT" << eom;

  status() << "Computing abstract LDG" << eom;
   
  ldgt ldg;
 
  ldg.compute(abstract_netlist);
    
  status() << "Computing CT" << eom;

  unsigned ct=::compute_ct(ldg);

  result() << "CT=" << ct << eom;

  return ct;
}

/*******************************************************************\

Function: bmc_cegart::cegar_loop

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void bmc_cegart::cegar_loop()
{
  initial_abstraction=true;

  while(true)
  {
    abstract();
    
    unsigned ct=compute_ct();

    if(ct>=MAX_CT)
    {
      error() << "CT too big -- giving up" << eom;
      throw 0;
    }
    
    // this is enough
    unsigned bound=ct;
    
    if(verify(bound))
    {
      status() << "VERIFICATION SUCCESSFUL -- PROPERTY HOLDS" << eom;
      return;
    }

    if(simulate(bound))
    {
      status() << "VERIFICATION FAILED -- PROPERTY REFUTED" << eom;
      return;
    }

    refine();
  }
}

/*******************************************************************\

Function: do_bmc_cegar

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

int do_bmc_cegar(
  const netlistt &netlist,
  ebmc_propertiest &properties,
  const namespacet &ns,
  message_handlert &message_handler)
{
  bmc_cegart bmc_cegar(netlist, properties, ns, message_handler);

  bmc_cegar.bmc_cegar();
  return 0;
}
