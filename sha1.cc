// Copyright (C) 2006 Nathaniel Smith <njs@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

// This file holds a registry of different SHA-1 implementations, and lets us
// benchmark them.

#include "base.hh"
#include <map>
#include <botan/engine.h>
#include <botan/libstate.h>

#include "sha1.hh"
#include "sha1_engine.hh"
#include "safe_map.hh"
#include "sanity.hh"
#include "ui.hh"
#include "platform.hh"
#include "cmd.hh"
#include "transforms.hh"

using std::map;
using std::pair;
using std::make_pair;
using std::string;

namespace
{
  map<int, pair<string, sha1_maker *> > & registry()
  {
    static map<int, pair<string, sha1_maker *> > the_registry;
    return the_registry;
  }

  void
  register_sha1(int priority, std::string const & name, sha1_maker * maker)
  {
    // invert priority, so that high priority sorts first (could override the
    // comparison function too, but this takes 1 character...)
    safe_insert(registry(), make_pair(-priority, make_pair(name, maker)));
  }

  sha1_maker * maker_to_be_benchmarked = 0;

  class Monotone_SHA1_Engine : public Botan::Engine
  {
  public:
    Botan::HashFunction * find_hash(const std::string& name) const
    {
      if (name == "SHA-160")
        {
          if (maker_to_be_benchmarked)
            {
              // We are in the middle of a benchmark run, so call the maker we
              // are supposed to be benchmarking.
              Botan::HashFunction * retval = maker_to_be_benchmarked();
              maker_to_be_benchmarked = 0;
              return retval;
            }
          else
            {
              I(!registry().empty());
              // Call the highest priority maker.
              return registry().begin()->second.second();
            }
        }
      return 0;
    }
  };

  // returning 0 from find_hash means that we don't want to handle this, and
  // causes Botan to drop through to its built-in, portable engine.
  Botan::HashFunction * botan_default_maker()
  {
    return 0;
  }
  sha1_registerer botan_default(0, "botan", &botan_default_maker);
}

sha1_registerer::sha1_registerer(int priority, string const & name, sha1_maker * maker)
{
  register_sha1(priority, name, maker);
}

void hook_botan_sha1()
{
  Botan::global_state().add_engine(new Monotone_SHA1_Engine);
}

CMD_HIDDEN(benchmark_sha1, "benchmark_sha1", "", CMD_REF(debug), "",
           N_("Benchmarks SHA-1 cores"),
           "",
           options::opts::none)
{
  P(F("Benchmarking %s SHA-1 cores") % registry().size());
  int mebibytes = 100;
  string test_str(mebibytes << 20, 'a');
  data test_data(test_str);
  for (map<int, pair<string, sha1_maker*> >::const_iterator i = registry().begin();
       i != registry().end(); ++i)
    {
      maker_to_be_benchmarked = i->second.second;
      id foo;
      double start = cpu_now();
      calculate_ident(test_data, foo);
      double end = cpu_now();
      double mebibytes_per_sec = mebibytes / (end - start);
      P(F("%s: %s MiB/s") % i->second.first % mebibytes_per_sec);
    }
}


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

