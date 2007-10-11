// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include "selectors.hh"
#include "sanity.hh"
#include "app_state.hh"
#include "constants.hh"

#include <boost/tokenizer.hpp>

using std::make_pair;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace selectors
{

  static void
  decode_selector(string const & orig_sel,
                  selector_type & type,
                  string & sel,
                  app_state & app)
  {
    sel = orig_sel;

    L(FL("decoding selector '%s'") % sel);

    string tmp;
    if (sel.size() < 2 || sel[1] != ':')
      {
        if (!app.lua.hook_expand_selector(sel, tmp))
          {
            L(FL("expansion of selector '%s' failed") % sel);
          }
        else
          {
            P(F("expanded selector '%s' -> '%s'") % sel % tmp);
            sel = tmp;
          }
      }

    if (sel.size() >= 2 && sel[1] == ':')
      {
        switch (sel[0])
          {
          case 'a':
            type = sel_author;
            break;
          case 'b':
            type = sel_branch;
            break;
          case 'h':
            type = sel_head;
            break;
          case 'd':
            type = sel_date;
            break;
          case 'i':
            type = sel_ident;
            break;
          case 't':
            type = sel_tag;
            break;
          case 'c':
            type = sel_cert;
            break;
          case 'l':
            type = sel_later;
            break;
          case 'e':
            type = sel_earlier;
            break;
          case 'p':
            type = sel_parent;
            break;
          default:
            W(F("unknown selector type: %c") % sel[0]);
            break;
          }
        sel.erase(0,2);

        /* a selector date-related should be validated */	
        if (sel_date==type || sel_later==type || sel_earlier==type)
          {
            if (app.lua.hook_exists("expand_date"))
              { 
                N(app.lua.hook_expand_date(sel, tmp),
                  F("selector '%s' is not a valid date\n") % sel);
              }
            else
              {
                // if expand_date is not available, start with something
                tmp = sel;
              }

            // if we still have a too short datetime string, expand it with
            // default values, but only if the type is earlier or later;
            // for searching a specific date cert this makes no sense
            // FIXME: this is highly speculative if expand_date wasn't called
            // beforehand - tmp could be _anything_ but a partial date string
            if (tmp.size()<8 && (sel_later==type || sel_earlier==type))
              tmp += "-01T00:00:00";
            else if (tmp.size()<11 && (sel_later==type || sel_earlier==type))
              tmp += "T00:00:00";
            N(tmp.size()==19 || sel_date==type, 
              F("selector '%s' is not a valid date (%s)") % sel % tmp);
            
            if (sel != tmp)
              {
                P (F ("expanded date '%s' -> '%s'\n") % sel % tmp);
                sel = tmp;
              }
          }
      }
  }

  void
  complete_selector(string const & orig_sel,
                    vector<pair<selector_type, string> > const & limit,
                    selector_type & type,
                    set<string> & completions,
                    app_state & app)
  {
    string sel;
    decode_selector(orig_sel, type, sel, app);
    app.db.complete(type, sel, limit, completions);
  }

  vector<pair<selector_type, string> >
  parse_selector(string const & str,
                 app_state & app)
  {
    vector<pair<selector_type, string> > sels;

    // this rule should always be enabled, even if the user specifies
    // --norc: if you provide a revision id, you get a revision id.
    if (str.find_first_not_of(constants::legal_id_bytes) == string::npos
        && str.size() == constants::idlen)
      {
        sels.push_back(make_pair(sel_ident, str));
      }
    else
      {
        typedef boost::tokenizer<boost::escaped_list_separator<char> > tokenizer;
        boost::escaped_list_separator<char> slash("\\", "/", "");
        tokenizer tokens(str, slash);

        vector<string> selector_strings;
        copy(tokens.begin(), tokens.end(), back_inserter(selector_strings));

        for (vector<string>::const_iterator i = selector_strings.begin();
             i != selector_strings.end(); ++i)
          {
            string sel;
            selector_type type = sel_unknown;

            decode_selector(*i, type, sel, app);
            sels.push_back(make_pair(type, sel));
          }
      }

    return sels;
  }

}; // namespace selectors

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
