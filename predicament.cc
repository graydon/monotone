// copyright (C) 2004 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <boost/shared_ptr.hpp>
#include <vector>

#include "app_state.hh"
#include "predicament.hh"
#include "sanity.hh"

void
predicament::solve()
{
  if (problems.empty())
    active = false;
  else
    {
      P(F("encountered %d problems during '%s'") % problems.size() % name);
      bool still_active = false;
      for (std::vector<problem>::iterator prob = problems.begin();
	   prob != problems.end(); ++prob)
	{
	  std::string sol;
	  if (!app.lua.hook_get_problem_solution(*prob, sol) 
	      || (prob->solutions.find(sol) == prob->solutions.end()))
	    {
	      throw informative_failure((F("unresolved problem: %s") % prob->name).str());
	    }

	  boost::shared_ptr<solution> s = prob->solutions[sol];
	  if (s->apply())
	    {
	      P(F("problem '%s' solved by solution '%s\n") % prob->name % s->name);
	    }
	  else
	    {
	      P(F("problem '%s' not solved by solution '%s\n") % prob->name % s->name);
	      still_active = true;
	    }
	}
      active = still_active;
      problems.clear();
    }
}
