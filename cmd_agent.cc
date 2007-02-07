#include <boost/scoped_ptr.hpp>

#include "cmd.hh"
#include "ssh_agent.hh"

using std::string;
using std::vector;
using boost::scoped_ptr;

static void
agent_list(string const & name, app_state & app, vector<utf8> const & args)
{
  if (args.size() != 0)
    throw usage(name);

  scoped_ptr<ssh_agent> a(new ssh_agent());
  a->connect();
  a->get_keys();
}

CMD(agent, N_("informative"),
    N_("list"),
    N_("interact with the agent"),
    options::opts::depth | options::opts::exclude)
{
  if (args.size() == 0)
    throw usage(name);

    vector<utf8>::const_iterator i = args.begin();
  ++i;
  vector<utf8> removed (i, args.end());
  if (idx(args, 0)() == "list")
    agent_list(name, app, removed);
  else
    throw usage(name);
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
