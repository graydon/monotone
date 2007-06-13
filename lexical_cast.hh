#ifndef __LEXICAL_CAST_HH__
#define __LEXICAL_CAST_HH__

// Copyright (C) 2007 Timothy Brownawell <tbrownaw@gmail.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.


#include <boost/lexical_cast.hpp>

// Generic lexical_cast can be a bit slow sometimes. If a particular
// version shows up in profiles, consider writing a specialization.
// Note: because we do this, every file that uses boost::lexical_cast
// _must_ include this file instead of <boost/lexical_cast.hpp>, or we
// risk violating the One Definition Rule (if some file instantiates
// the generic template for the types we specialize here).  This is not
// a theoretical problem; the Windows linker will fail.

namespace boost {
  template<>
  std::string lexical_cast<std::string, unsigned int>(unsigned int const & _i);

  template<>
  unsigned int lexical_cast<unsigned int, std::string>(std::string const & s);

}

#endif

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
