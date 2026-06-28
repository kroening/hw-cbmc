/*******************************************************************\

Module: Command Line Parsing

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef VLSIM_PARSEOPTIONS_H
#define VLSIM_PARSEOPTIONS_H

#include <util/parse_options.h>
#include <util/ui_message.h>

#include <ebmc/ebmc_version.h>

class vlsim_parse_optionst : public parse_options_baset
{
public:
  int doit() override;
  void help() override;

  vlsim_parse_optionst(int argc, const char **argv)
    : parse_options_baset(
        "(version)"
        "(show-parse)(preprocess)"
        "(module):(top):"
        "(systemverilog)"
        "(warn-implicit-nets)"
        "(verbosity):"
        "I:D:",
        argc,
        argv,
        std::string("VLSIM ") + EBMC_VERSION),
      ui_message_handler(cmdline, std::string("VLSIM ") + EBMC_VERSION)
  {
  }

  ~vlsim_parse_optionst() override = default;

protected:
  ui_message_handlert ui_message_handler;
};

#endif
