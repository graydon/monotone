// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <limits>
#include <sstream>
#include "vector.hh"

#include <boost/shared_ptr.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#include "lexical_cast.hh"
#include "cert.hh"
#include "constants.hh"
#include "database.hh"
#include "interner.hh"
#include "keys.hh"
#include "key_store.hh"
#include "netio.hh"
#include "options.hh"
#include "project.hh"
#include "revision.hh"
#include "sanity.hh"
#include "simplestring_xform.hh"
#include "transforms.hh"
#include "ui.hh"

using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;
using std::remove_if;

using boost::shared_ptr;
using boost::get;
using boost::tuple;
using boost::lexical_cast;

// The alternaive is to #include "cert.hh" in vocab.*, which is even
// uglier.

#include "vocab_macros.hh"
cc_DECORATE(revision)
cc_DECORATE(manifest)
template <typename T>
static inline void
verify(T & val)
{}
template class revision<cert>;
template class manifest<cert>;

// FIXME: the bogus-cert family of functions is ridiculous
// and needs to be replaced, or at least factored.

struct
bogus_cert_p
{
  database & db;
  bogus_cert_p(database & db) : db(db) {};

  bool cert_is_bogus(cert const & c) const
  {
    cert_status status = check_cert(db, c);
    if (status == cert_ok)
      {
        L(FL("cert ok"));
        return false;
      }
    else if (status == cert_bad)
      {
        string txt;
        cert_signable_text(c, txt);
        W(F("ignoring bad signature by '%s' on '%s'") % c.key() % txt);
        return true;
      }
    else
      {
        I(status == cert_unknown);
        string txt;
        cert_signable_text(c, txt);
        W(F("ignoring unknown signature by '%s' on '%s'") % c.key() % txt);
        return true;
      }
  }

  bool operator()(revision<cert> const & c) const
  {
    return cert_is_bogus(c.inner());
  }

  bool operator()(manifest<cert> const & c) const
  {
    return cert_is_bogus(c.inner());
  }
};


void
erase_bogus_certs(database & db,
                  vector< manifest<cert> > & certs)
{
  typedef vector< manifest<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(db));
  certs.erase(e, certs.end());

  vector< manifest<cert> > tmp_certs;

  // Sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, cert_name, cert_value > trust_key;
  typedef map< trust_key, 
    pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, 
                                i->inner().name, 
                                i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
        {
          s.reset(new set<rsa_keypair_id>());
          trust.insert(make_pair(key, make_pair(s, i)));
        }
      else
        s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      if (db.hook_get_manifest_cert_trust(*(i->second.first),
                                          get<0>(i->first),
                                          get<1>(i->first),
                                          get<2>(i->first)))
        {
          L(FL("trust function liked %d signers of %s cert on manifest %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
          tmp_certs.push_back(*(i->second.second));
        }
      else
        {
          W(F("trust function disliked %d signers of %s cert on manifest %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
        }
    }
  certs = tmp_certs;
}

void
erase_bogus_certs(database & db,
                  vector< revision<cert> > & certs)
{
  typedef vector< revision<cert> >::iterator it;
  it e = remove_if(certs.begin(), certs.end(), bogus_cert_p(db));
  certs.erase(e, certs.end());

  vector< revision<cert> > tmp_certs;

  // sorry, this is a crazy data structure
  typedef tuple< hexenc<id>, cert_name, cert_value > trust_key;
  typedef map< trust_key, 
    pair< shared_ptr< set<rsa_keypair_id> >, it > > trust_map;
  trust_map trust;

  for (it i = certs.begin(); i != certs.end(); ++i)
    {
      trust_key key = trust_key(i->inner().ident, 
                                i->inner().name, 
                                i->inner().value);
      trust_map::iterator j = trust.find(key);
      shared_ptr< set<rsa_keypair_id> > s;
      if (j == trust.end())
        {
          s.reset(new set<rsa_keypair_id>());
          trust.insert(make_pair(key, make_pair(s, i)));
        }
      else
        s = j->second.first;
      s->insert(i->inner().key);
    }

  for (trust_map::const_iterator i = trust.begin();
       i != trust.end(); ++i)
    {
      if (db.hook_get_revision_cert_trust(*(i->second.first),
                                          get<0>(i->first),
                                          get<1>(i->first),
                                          get<2>(i->first)))
        {
          L(FL("trust function liked %d signers of %s cert on revision %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
          tmp_certs.push_back(*(i->second.second));
        }
      else
        {
          W(F("trust function disliked %d signers of %s cert on revision %s")
            % i->second.first->size() % get<1>(i->first) % get<0>(i->first));
        }
    }
  certs = tmp_certs;
}


// cert-managing routines

cert::cert()
{}

cert::cert(std::string const & s)
{
  read_cert(s, *this);
}

cert::cert(hexenc<id> const & ident,
           cert_name const & name,
           cert_value const & value,
           rsa_keypair_id const & key)
  : ident(ident), name(name), value(value), key(key)
{}

cert::cert(hexenc<id> const & ident,
         cert_name const & name,
         cert_value const & value,
         rsa_keypair_id const & key,
         rsa_sha1_signature const & sig)
  : ident(ident), name(name), value(value), key(key), sig(sig)
{}

bool
cert::operator<(cert const & other) const
{
  return (ident < other.ident)
    || ((ident == other.ident) && name < other.name)
    || (((ident == other.ident) && name == other.name)
        && value < other.value)
    || ((((ident == other.ident) && name == other.name)
         && value == other.value) && key < other.key)
    || (((((ident == other.ident) && name == other.name)
          && value == other.value) && key == other.key) && sig < other.sig);
}

bool
cert::operator==(cert const & other) const
{
  return
    (ident == other.ident)
    && (name == other.name)
    && (value == other.value)
    && (key == other.key)
    && (sig == other.sig);
}

// netio support

void
read_cert(string const & in, cert & t)
{
  size_t pos = 0;
  id hash = id(extract_substring(in, pos,
                                 constants::merkle_hash_length_in_bytes,
                                 "cert hash"));
  id ident = id(extract_substring(in, pos,
                                  constants::merkle_hash_length_in_bytes,
                                  "cert ident"));
  string name, val, key, sig;
  extract_variable_length_string(in, name, pos, "cert name");
  extract_variable_length_string(in, val, pos, "cert val");
  extract_variable_length_string(in, key, pos, "cert key");
  extract_variable_length_string(in, sig, pos, "cert sig");
  assert_end_of_buffer(in, pos, "cert");

  hexenc<id> hid;
  encode_hexenc(ident, hid);
  cert tmp(hid, cert_name(name), cert_value(val), rsa_keypair_id(key),
           rsa_sha1_signature(sig));

  hexenc<id> hcheck;
  id check;
  cert_hash_code(tmp, hcheck);
  decode_hexenc(hcheck, check);
  if (!(check == hash))
    {
      hexenc<id> hhash;
      encode_hexenc(hash, hhash);
      throw bad_decode(F("calculated cert hash '%s' does not match '%s'")
                       % hcheck % hhash);
    }
  t = tmp;
}

void
write_cert(cert const & t, string & out)
{
  string name, key;
  hexenc<id> hash;
  id ident_decoded, hash_decoded;

  cert_hash_code(t, hash);
  decode_hexenc(t.ident, ident_decoded);
  decode_hexenc(hash, hash_decoded);

  out.append(hash_decoded());
  out.append(ident_decoded());
  insert_variable_length_string(t.name(), out);
  insert_variable_length_string(t.value(), out);
  insert_variable_length_string(t.key(), out);
  insert_variable_length_string(t.sig(), out);
}

void
cert_signable_text(cert const & t, string & out)
{
  base64<cert_value> val_encoded(encode_base64(t.value));

  out.clear();
  out.reserve(4 + t.name().size() + t.ident().size()
              + val_encoded().size());

  out += '[';
  out.append(t.name());
  out += '@';
  out.append(t.ident());
  out += ':';
  append_without_ws(out, val_encoded());
  out += ']';

  L(FL("cert: signable text %s") % out);
}

void
cert_hash_code(cert const & t, hexenc<id> & out)
{
  base64<rsa_sha1_signature> sig_encoded(encode_base64(t.sig));
  base64<cert_value> val_encoded(encode_base64(t.value));
  string tmp;
  tmp.reserve(4+t.ident().size() + t.name().size() + val_encoded().size() +
              t.key().size() + sig_encoded().size());
  tmp.append(t.ident());
  tmp += ':';
  tmp.append(t.name());
  tmp += ':';
  append_without_ws(tmp, val_encoded());
  tmp += ':';
  tmp.append(t.key());
  tmp += ':';
  append_without_ws(tmp, sig_encoded());

  data tdat(tmp);
  calculate_ident(tdat, out);
}

cert_status
check_cert(database & db, cert const & t)
{
  string signed_text;
  cert_signable_text(t, signed_text);
  return db.check_signature(t.key, signed_text, t.sig);
}

bool
put_simple_revision_cert(database & db,
                         key_store & keys,
                         revision_id const & id,
                         cert_name const & nm,
                         cert_value const & val)
{
  I(!keys.signing_key().empty());

  cert t(id.inner(), nm, val, keys.signing_key);
  string signed_text;
  cert_signable_text(t, signed_text);
  load_key_pair(keys, t.key);
  keys.make_signature(db, t.key, signed_text, t.sig);

  revision<cert> cc(t);
  return db.put_revision_cert(cc);
}

// "special certs"

// Guess which branch is appropriate for a commit below IDENT.
// OPTS may override.  Branch name is returned in BRANCHNAME.
// Does not modify branch state in OPTS.
void
guess_branch(options & opts, project_t & project,
             revision_id const & ident, branch_name & branchname)
{
  if (opts.branch_given && !opts.branchname().empty())
    branchname = opts.branchname;
  else
    {
      N(!ident.inner()().empty(),
        F("no branch found for empty revision, "
          "please provide a branch name"));

      set<branch_name> branches;
      project.get_revision_branches(ident, branches);

      N(branches.size() != 0,
        F("no branch certs found for revision %s, "
          "please provide a branch name") % ident);

      N(branches.size() == 1,
        F("multiple branch certs found for revision %s, "
          "please provide a branch name") % ident);

      set<branch_name>::iterator i = branches.begin();
      I(i != branches.end());
      branchname = *i;
    }
}

// As above, but set the branch name in the options
// if it wasn't already set.
void
guess_branch(options & opts, project_t & project, revision_id const & ident)
{
  branch_name branchname;
  guess_branch(opts, project, ident, branchname);
  opts.branchname = branchname;
}

void
cert_revision_in_branch(database & db,
                        key_store & keys,
                        revision_id const & rev,
                        branch_name const & branch)
{
  put_simple_revision_cert(db, keys, rev, branch_cert_name,
                           cert_value(branch()));
}

void
cert_revision_suspended_in_branch(database & db,
                                  key_store & keys,
                                  revision_id const & rev,
                                  branch_name const & branch)
{
  put_simple_revision_cert(db, keys, rev, suspend_cert_name,
                           cert_value(branch()));
}


// "standard certs"

void
cert_revision_date_time(database & db,
                        key_store & keys,
                        revision_id const & rev,
                        date_t const & t)
{
  cert_value val = cert_value(t.as_iso_8601_extended());
  put_simple_revision_cert(db, keys, rev, date_cert_name, val);
}

void
cert_revision_author(database & db,
                     key_store & keys,
                     revision_id const & rev,
                     string const & author)
{
  put_simple_revision_cert(db, keys, rev, author_cert_name,
                           cert_value(author));
}

void
cert_revision_tag(database & db,
                  key_store & keys,
                  revision_id const & rev,
                  string const & tagname)
{
  put_simple_revision_cert(db, keys, rev, tag_cert_name,
                           cert_value(tagname));
}

void
cert_revision_changelog(database & db,
                        key_store & keys,
                        revision_id const & rev,
                        utf8 const & log)
{
  put_simple_revision_cert(db, keys, rev, changelog_cert_name,
                           cert_value(log()));
}

void
cert_revision_comment(database & db,
                      key_store & keys,
                      revision_id const & rev,
                      utf8 const & comment)
{
  put_simple_revision_cert(db, keys, rev, comment_cert_name,
                           cert_value(comment()));
}

void
cert_revision_testresult(database & db,
                         key_store & keys,
                         revision_id const & rev,
                         string const & results)
{
  bool passed = false;
  if (lowercase(results) == "true" ||
      lowercase(results) == "yes" ||
      lowercase(results) == "pass" ||
      results == "1")
    passed = true;
  else if (lowercase(results) == "false" ||
           lowercase(results) == "no" ||
           lowercase(results) == "fail" ||
           results == "0")
    passed = false;
  else
    throw informative_failure("could not interpret test results, "
                              "tried '0/1' 'yes/no', 'true/false', "
                              "'pass/fail'");

  put_simple_revision_cert(db, keys, rev, testresult_cert_name,
                           cert_value(lexical_cast<string>(passed)));
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
