#ifndef __CERT_HH__
#define __CERT_HH__

// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include "vocab.hh"

#include <set>
#include <map>
#include <vector>
#include <time.h>
#include <boost/date_time/posix_time/posix_time.hpp>

// certs associate an opaque name/value pair with a particular identifier in
// the system (eg. a manifest or file id) and are accompanied by an RSA
// public-key signature attesting to the association. users can write as
// much extra meta-data as they like about files or manifests, using certs,
// without needing anyone's special permission.

struct app_state;
struct packet_consumer;

struct cert
{
  cert();
  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key);
  cert(hexenc<id> const & ident,
      cert_name const & name,
      base64<cert_value> const & value,
      rsa_keypair_id const & key,
      base64<rsa_sha1_signature> const & sig);
  hexenc<id> ident;
  cert_name name;
  base64<cert_value> value;
  rsa_keypair_id key;
  base64<rsa_sha1_signature> sig;
  bool operator<(cert const & other) const;
  bool operator==(cert const & other) const;
};


// these 3 are for netio support
void read_cert(std::string const & in, cert & t);
void write_cert(cert const & t, std::string & out);
void cert_hash_code(cert const & t, hexenc<id> & out);

typedef enum {cert_ok, cert_bad, cert_unknown} cert_status;

void cert_signable_text(cert const & t,std::string & out);
cert_status check_cert(app_state & app, cert const & t);
bool priv_key_exists(app_state & app, rsa_keypair_id const & id);
void load_key_pair(app_state & app,
                   rsa_keypair_id const & id,
                   keypair & kp);
void calculate_cert(app_state & app, cert & t);
void make_simple_cert(hexenc<id> const & id,
                      cert_name const & nm,
                      cert_value const & cv,
                      app_state & app,
                      cert & c);

void erase_bogus_certs(std::vector< revision<cert> > & certs,
                       app_state & app);

void erase_bogus_certs(std::vector< manifest<cert> > & certs,
                       app_state & app);

// special certs -- system won't work without them

extern std::string const branch_cert_name;

void 
cert_revision_in_branch(revision_id const & ctx, 
                        cert_value const & branchname,
                        app_state & app,
                        packet_consumer & pc);

void 
get_branch_heads(cert_value const & branchname,
                 app_state & app,
                 std::set<revision_id> & heads);

// we also define some common cert types, to help establish useful
// conventions. you should use these unless you have a compelling
// reason not to.

// N()'s out if there is no unique key for us to use
void
get_user_key(rsa_keypair_id & key, app_state & app);

void 
guess_branch(revision_id const & id,
             app_state & app,
             cert_value & branchname);

extern std::string const date_cert_name;
extern std::string const author_cert_name;
extern std::string const tag_cert_name;
extern std::string const changelog_cert_name;
extern std::string const comment_cert_name;
extern std::string const testresult_cert_name;

void 
cert_revision_date_now(revision_id const & m, 
                      app_state & app,
                      packet_consumer & pc);

void 
cert_revision_date_time(revision_id const & m, 
                        boost::posix_time::ptime t,
                        app_state & app,
                        packet_consumer & pc);

void 
cert_revision_date_time(revision_id const & m, 
                        time_t time,
                        app_state & app,
                        packet_consumer & pc);

void 
cert_revision_author(revision_id const & m, 
                    std::string const & author,
                    app_state & app,
                    packet_consumer & pc);

void 
cert_revision_author_default(revision_id const & m, 
                            app_state & app,
                            packet_consumer & pc);

void 
cert_revision_tag(revision_id const & m, 
                 std::string const & tagname,
                 app_state & app,
                 packet_consumer & pc);

void 
cert_revision_changelog(revision_id const & m, 
                       std::string const & changelog,
                       app_state & app,
                       packet_consumer & pc);

void 
cert_revision_comment(revision_id const & m, 
                     std::string const & comment,
                     app_state & app,
                     packet_consumer & pc);

void 
cert_revision_testresult(revision_id const & m, 
                         std::string const & results,
                         app_state & app,
                         packet_consumer & pc);


#endif // __CERT_HH__
