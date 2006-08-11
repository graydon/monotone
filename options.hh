#ifndef __OPTIONS_HH__
#define __OPTIONS_HH__

// Copyright (C) 2005 Richard Levitte <richard@levitte.org>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>

namespace option
{
  using boost::program_options::option_description;
  using boost::program_options::options_description;
  using boost::shared_ptr;
#define OPT_STDIO 49
#define OPT_NO_TRANSPORT_AUTH 50
#define OPT_NO_SHOW_ENCLOSER 51
#define OPT_AUTOMATE_STDIO_SIZE 52

  extern options_description global_options;
  extern options_description specific_options;

  struct option
  {
    char const * operator()() { return o->long_name().c_str(); }
    operator shared_ptr<option_description> () { return o; }
  protected:
    option(option_description * p) : o(p) {}
    shared_ptr<option_description> o;
  };

  struct global : public option
  {
    global(option_description * p) : option(p) { global_options.add(o); }
  };

  struct specific : public option
  {
    specific(option_description * p) : option(p) { specific_options.add(o); }
  };

  struct no_option
  {
  };

  extern no_option none;

  // global options
  extern global argfile;
  extern global conf_dir;
  extern global db_name;
  extern global debug;
  extern global dump;
  extern global full_version;
  extern global help;
  extern global key_dir;
  extern global key_name;
  extern global log;
  extern global norc;
  extern global nostd;
  extern global quiet;
  extern global rcfile;
  extern global reallyquiet;
  extern global root;
  extern global ticker;
  extern global verbose;
  extern global version;

  // command-specific options
  extern specific author;
  extern specific bind;
  extern specific branch_name;
  extern specific brief;
  extern specific context_diff;
  extern specific date;
  extern specific depth;
  extern specific diffs;
  extern specific drop_attr;
  extern specific exclude;
  extern specific execute;
  extern specific external_diff;
  extern specific external_diff_args;
  extern specific key_to_push;
  extern specific last;
  extern specific message;
  extern specific missing;
  extern specific msgfile;
  extern specific next;
  extern specific no_files;
  extern specific no_merges;
  extern specific pidfile;
  extern specific recursive;
  extern specific revision;
  extern specific set_default;
  extern specific unified_diff;
  extern specific unknown;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __OPTIONS_HH__
