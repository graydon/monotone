// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include "base.hh"
#include "lexical_cast.hh"

template<>
std::string boost::lexical_cast<std::string, unsigned int>(unsigned int const & _i)
{
  unsigned int i = _i;
  if (!i)
    return std::string("0");

  unsigned int const maxlen = sizeof(unsigned int) * 3;
  char buf[1+maxlen];
  buf[maxlen] = '\0';

  unsigned int pos = maxlen;

  while (i && pos <= maxlen)
    {
      --pos;
      buf[pos] = ('0' + (i%10));
      i /= 10;
    }
  return std::string(buf+pos);
}

template<>
unsigned int boost::lexical_cast<unsigned int, std::string>(std::string const & s)
{
  unsigned int out = 0;
  std::string::const_iterator i;
  for (i = s.begin(); i != s.end() && (unsigned int)(*i - '0') < 10; ++i)
    {
      out = out*10 + (*i - '0');
    }
  if (i != s.end())
    throw boost::bad_lexical_cast();
  return out;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
