#ifndef __CERT_HH__
#define __CERT_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include <map>
#include <set>
#include "vector.hh"

#include "vocab.hh"
#include "dates.hh"

// Certs associate an opaque name/value pair with a revision ID, and
// are accompanied by an RSA public-key signature attesting to the
// association. Users can write as much extra meta-data as they like
// about revisions, using certs, without needing anyone's special
// permission.

class key_store;
class database;
class project_t;
struct options;

struct cert
{
  cert();

  // This is to make revision<cert> and manifest<cert> work.
  explicit cert(std::string const & s);

  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key);
  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key,
      rsa_sha1_signature const & sig);
  hexenc<id> ident;
  cert_name name;
  base64<cert_value> value;
  rsa_keypair_id key;
  rsa_sha1_signature sig;
  bool operator<(cert const & other) const;
  bool operator==(cert const & other) const;
};

EXTERN template class revision<cert>;
EXTERN template class manifest<cert>;


// These 3 are for netio support.
void read_cert(std::string const & in, cert & t);
void write_cert(cert const & t, std::string & out);
void cert_hash_code(cert const & t, hexenc<id> & out);

typedef enum {cert_ok, cert_bad, cert_unknown} cert_status;

void cert_signable_text(cert const & t,std::string & out);
cert_status check_cert(database & db, cert const & t);

bool put_simple_revision_cert(database & db,
                              key_store & keys,
                              revision_id const & id,
                              cert_name const & nm,
                              cert_value const & val);

void erase_bogus_certs(database & db, std::vector< revision<cert> > & certs);
void erase_bogus_certs(database & db, std::vector< manifest<cert> > & certs);

// Special certs -- system won't work without them.

#define branch_cert_name cert_name("branch")

void
cert_revision_in_branch(database & db, key_store & keys,
                        revision_id const & rev,
                        branch_name const & branchname);


// We also define some common cert types, to help establish useful
// conventions. you should use these unless you have a compelling
// reason not to.

void
guess_branch(options & opts, project_t & project, revision_id const & rev,
             branch_name & branchname);
void
guess_branch(options & opts, project_t & project, revision_id const & rev);

#define date_cert_name cert_name("date")
#define author_cert_name cert_name("author")
#define tag_cert_name cert_name("tag")
#define changelog_cert_name cert_name("changelog")
#define comment_cert_name cert_name("comment")
#define testresult_cert_name cert_name("testresult")
#define suspend_cert_name cert_name("suspend")

void
cert_revision_suspended_in_branch(database & db, key_store & keys,
                                  revision_id const & rev,
                                  branch_name const & branchname);

void
cert_revision_date_time(database & db, key_store & keys,
                        revision_id const & rev,
                        date_t const & t);

void
cert_revision_author(database & db, key_store & keys,
                     revision_id const & m,
                     std::string const & author);

void
cert_revision_tag(database & db, key_store & keys,
                  revision_id const & rev,
                  std::string const & tagname);

void
cert_revision_changelog(database & db, key_store & keys,
                        revision_id const & rev,
                        utf8 const & changelog);

void
cert_revision_comment(database & db, key_store & keys,
                      revision_id const & m,
                      utf8 const & comment);

void
cert_revision_testresult(database & db, key_store & keys,
                         revision_id const & m,
                         std::string const & results);


// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif // __CERT_HH__
