#include <string>
#include "ssh_agent_platform.hh"

using std::string;

bool
ssh_agent_platform::connect() 
{
  return false;
}

bool
ssh_agent_platform::disconnect()
{
  return false;
}


bool
ssh_agent_platform::connected()
{
  return false;
}

void
ssh_agent_platform::write_data(string const & data)
{
}

void 
ssh_agent_platform::read_data(u32 const len, string & out)
{
}
