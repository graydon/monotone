// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

#include <iostream>
#include <string>

#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>

#include "app_state.hh"
#include "cset.hh"
#include "constants.hh"
#include "packet.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "keys.hh"

using namespace std;
using boost::shared_ptr;
using boost::lexical_cast;
using boost::match_default;
using boost::match_results;
using boost::regex;

void
packet_consumer::set_on_revision_written(boost::function1<void,
                                         revision_id> const & x)
{
  on_revision_written=x;
}

void
packet_consumer::set_on_cert_written(boost::function1<void,
                                     cert const &> const & x)
{
  on_cert_written=x;
}

void
packet_consumer::set_on_pubkey_written(boost::function1<void, rsa_keypair_id>
                                       const & x)
{
  on_pubkey_written=x;
}

void
packet_consumer::set_on_keypair_written(boost::function1<void, rsa_keypair_id>
                                        const & x)
{
  on_keypair_written=x;
}


packet_db_writer::packet_db_writer(app_state & app) 
  : app(app)
{}

packet_db_writer::~packet_db_writer() 
{}

void 
packet_db_writer::consume_file_data(file_id const & ident, 
                                    file_data const & dat)
{
  if (app.db.file_version_exists(ident))
    {
      L(FL("file version '%s' already exists in db\n") % ident);
      return;
    }

  transaction_guard guard(app.db);
  app.db.put_file(ident, dat);
  guard.commit();
}

void 
packet_db_writer::consume_file_delta(file_id const & old_id, 
                                     file_id const & new_id,
                                     file_delta const & del)
{
  transaction_guard guard(app.db);

  if (app.db.file_version_exists(new_id))
    {
      L(FL("file version '%s' already exists in db\n") % new_id);
      return;
    }

  if (!app.db.file_version_exists(old_id))
    {
      W(F("file preimage '%s' missing in db") % old_id);
      W(F("dropping delta '%s' -> '%s'") % old_id % new_id);
      return;
    }

  file_id confirm;
  file_data old_dat;
  data new_dat;
  app.db.get_file_version(old_id, old_dat);
  patch(old_dat.inner(), del.inner(), new_dat);
  calculate_ident(file_data(new_dat), confirm);
  if (confirm == new_id)
    app.db.put_file_version(old_id, new_id, del);
  else
    {
      W(F("reconstructed file from delta '%s' -> '%s' has wrong id '%s'\n") 
        % old_id % new_id % confirm);
    }

  guard.commit();
}

void
packet_db_writer::consume_revision_data(revision_id const & ident, 
                                        revision_data const & dat)
{
  MM(ident);
  transaction_guard guard(app.db);
  if (app.db.revision_exists(ident))
    {
      L(FL("revision '%s' already exists in db\n") % ident);
      return;
    }

  revision_set rev;
      MM(rev);
  read_revision_set(dat, rev);
      
  for (edge_map::const_iterator i = rev.edges.begin(); 
       i != rev.edges.end(); ++i)
    {
      if (!edge_old_revision(i).inner()().empty() 
          && !app.db.revision_exists(edge_old_revision(i)))
        {
          W(F("missing prerequisite revision '%s'\n") % edge_old_revision(i));
          W(F("dropping revision '%s'\n") % ident);
          return;
        }
      
      for (std::map<split_path, file_id>::const_iterator a 
             = edge_changes(i).files_added.begin(); 
           a != edge_changes(i).files_added.end(); ++a)          
        {
          if (! app.db.file_version_exists(a->second))
            {
              W(F("missing prerequisite file '%s'\n") % a->second);
              W(F("dropping revision '%s'\n") % ident);
              return;
            }      
        }

      for (std::map<split_path, std::pair<file_id, file_id> >::const_iterator d 
             = edge_changes(i).deltas_applied.begin();
           d != edge_changes(i).deltas_applied.end(); ++d)
        {
          I(!delta_entry_src(d).inner()().empty());
          I(!delta_entry_dst(d).inner()().empty());

          if (! app.db.file_version_exists(delta_entry_src(d)))
            {
              W(F("missing prerequisite file pre-delta '%s'\n") 
                % delta_entry_src(d));
              W(F("dropping revision '%s'\n") % ident);
              return;
            }      
              
          if (! app.db.file_version_exists(delta_entry_dst(d)))
            {
              W(F("missing prerequisite file post-delta '%s'\n") 
                % delta_entry_dst(d));
              W(F("dropping revision '%s'\n") % ident);
              return;
            }
        }     
    }

  app.db.put_revision(ident, dat);
  if (on_revision_written) 
    on_revision_written(ident);
  guard.commit();
}

void 
packet_db_writer::consume_revision_cert(revision<cert> const & t)
{
  transaction_guard guard(app.db);

  if (app.db.revision_cert_exists(t))
    {
      L(FL("revision cert on '%s' already exists in db\n") 
        % t.inner().ident);
      return;
    }
  
  if (!app.db.revision_exists(revision_id(t.inner().ident)))
    {
      W(F("cert revision '%s' does not exist in db\n") 
        % t.inner().ident);
      W(F("dropping cert\n"));
      return;
    }

  app.db.put_revision_cert(t);
  if (on_cert_written) 
    on_cert_written(t.inner());

  guard.commit();
}


void 
packet_db_writer::consume_public_key(rsa_keypair_id const & ident,
                                     base64< rsa_pub_key > const & k)
{
  transaction_guard guard(app.db);

  if (app.db.public_key_exists(ident))
    {
      base64<rsa_pub_key> tmp;
      app.db.get_key(ident, tmp);
      if (!keys_match(ident, tmp, ident, k))
        W(F("key '%s' is not equal to key '%s' in database\n") % ident % ident);
      L(FL("skipping existing public key %s\n") % ident);
      return;
    }

  L(FL("putting public key %s\n") % ident);
  app.db.put_key(ident, k);
  if (on_pubkey_written) 
    on_pubkey_written(ident);

  guard.commit();
}

void 
packet_db_writer::consume_key_pair(rsa_keypair_id const & ident,
                                   keypair const & kp)
{
  transaction_guard guard(app.db);

  if (app.keys.key_pair_exists(ident))
    {
      L(FL("skipping existing key pair %s\n") % ident);
      return;
    }

  app.keys.put_key_pair(ident, kp);
  if (on_keypair_written) 
    on_keypair_written(ident);

  guard.commit();
}

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void 
packet_writer::consume_file_data(file_id const & ident, 
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_file_delta(file_id const & old_id, 
                                  file_id const & new_id,
                                  file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[fdelta " << old_id.inner()() << endl 
      << "        " << new_id.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_data(revision_id const & ident, 
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << ident.inner()() << "]" << endl 
      << trim_ws(packed()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_revision_cert(revision<cert> const & t)
{
  ost << "[rcert " << t.inner().ident() << endl
      << "       " << t.inner().name() << endl
      << "       " << t.inner().key() << endl
      << "       " << trim_ws(t.inner().value()) << "]" << endl
      << trim_ws(t.inner().sig()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k)
{
  ost << "[pubkey " << ident() << "]" << endl
      << trim_ws(k()) << endl
      << "[end]" << endl;
}

void 
packet_writer::consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp)
{
  ost << "[keypair " << ident() << "]" << endl
      << trim_ws(kp.pub()) <<"#\n" <<trim_ws(kp.priv()) << endl
      << "[end]" << endl;
}


// -- remainder just deals with the regexes for reading packets off streams

struct 
feed_packet_consumer
{
  app_state & app;
  size_t & count;
  packet_consumer & cons;
  std::string ident;
  std::string key;
  std::string certname;
  std::string base;
  std::string sp;
  feed_packet_consumer(size_t & count, packet_consumer & c, app_state & app_)
   : app(app_), count(count), cons(c),
     ident(constants::regex_legal_id_bytes),
     key(constants::regex_legal_key_name_bytes),
     certname(constants::regex_legal_cert_name_bytes),
     base(constants::regex_legal_packet_bytes),
     sp("[[:space:]]+")
  {}
  void require(bool x) const
  {
    E(x, F("malformed packet"));
  }
  bool operator()(match_results<std::string::const_iterator> const & res) const
  {
    if (res.size() != 4)
      throw oops("matched impossible packet with " 
                 + lexical_cast<string>(res.size()) + " matching parts: " +
                 string(res[0].first, res[0].second));
    I(res[1].matched);
    I(res[2].matched);
    I(res[3].matched);
    std::string type(res[1].first, res[1].second);
    std::string args(res[2].first, res[2].second);
    std::string body(res[3].first, res[3].second);
    if (regex_match(type, regex("[fr]data")))
      {
        L(FL("read data packet"));
        require(regex_match(args, regex(ident)));
        require(regex_match(body, regex(base)));
        base64<gzip<data> > body_packed(trim_ws(body));
        data contents;
        unpack(body_packed, contents);
        if (type == "rdata")
          cons.consume_revision_data(revision_id(hexenc<id>(args)), 
                                     revision_data(contents));
        else if (type == "fdata")
          cons.consume_file_data(file_id(hexenc<id>(args)), 
                                 file_data(contents));
        else
          throw oops("matched impossible data packet with head '" + type + "'");
      }
    else if (type == "fdelta")
      {
        L(FL("read delta packet"));
        match_results<std::string::const_iterator> matches;
        require(regex_match(args, matches, regex(ident + sp + ident)));
        string src_id(matches[1].first, matches[1].second);
        string dst_id(matches[2].first, matches[2].second);
        require(regex_match(body, regex(base)));
        base64<gzip<delta> > body_packed(trim_ws(body));
        delta contents;
        unpack(body_packed, contents);
        cons.consume_file_delta(file_id(hexenc<id>(src_id)), 
                                file_id(hexenc<id>(dst_id)), 
                                file_delta(contents));
      }
    else if (type == "rcert")
      {
        L(FL("read cert packet"));
        match_results<std::string::const_iterator> matches;
        require(regex_match(args, matches, regex(ident + sp + certname
                                                 + sp + key + sp + base)));
        string certid(matches[1].first, matches[1].second);
        string name(matches[2].first, matches[2].second);
        string keyid(matches[3].first, matches[3].second);
        string val(matches[4].first, matches[4].second);
        string contents(trim_ws(body));

        // canonicalize the base64 encodings to permit searches
        cert t = cert(hexenc<id>(certid),
                      cert_name(name),
                      base64<cert_value>(canonical_base64(val)),
                      rsa_keypair_id(keyid),
                      base64<rsa_sha1_signature>(canonical_base64(contents)));
        cons.consume_revision_cert(revision<cert>(t));
      } 
    else if (type == "pubkey")
      {
        L(FL("read pubkey data packet"));
        require(regex_match(args, regex(key)));
        require(regex_match(body, regex(base)));
        string contents(trim_ws(body));
        cons.consume_public_key(rsa_keypair_id(args),
                                base64<rsa_pub_key>(contents));
      }
    else if (type == "keypair")
      {
        L(FL("read keypair data packet"));
        require(regex_match(args, regex(key)));
        match_results<std::string::const_iterator> matches;
        require(regex_match(body, matches, regex(base + "#" + base)));
        string pub_dat(trim_ws(string(matches[1].first, matches[1].second)));
        string priv_dat(trim_ws(string(matches[2].first, matches[2].second)));
        cons.consume_key_pair(rsa_keypair_id(args), keypair(pub_dat, priv_dat));
      }
    else if (type == "privkey")
      {
        L(FL("read pubkey data packet"));
        require(regex_match(args, regex(key)));
        require(regex_match(body, regex(base)));
        string contents(trim_ws(body));
        keypair kp;
        migrate_private_key(app,
                            rsa_keypair_id(args),
                            base64<arc4<rsa_priv_key> >(contents),
                            kp);
        cons.consume_key_pair(rsa_keypair_id(args), kp);
      }
    else
      {
        W(F("unknown packet type: '%s'") % type);
        return true;
      }
    ++count;
    return true;
  }
};

static size_t 
extract_packets(string const & s, packet_consumer & cons, app_state & app)
{
  string const head("\\[([a-z]+)[[:space:]]+([^\\[\\]]+)\\]");
  string const body("([^\\[\\]]+)");
  string const tail("\\[end\\]");
  string const whole = head + body + tail;
  regex expr(whole);
  size_t count = 0;
  regex_grep(feed_packet_consumer(count, cons, app), s, expr, match_default);
  return count;
}


size_t 
read_packets(istream & in, packet_consumer & cons, app_state & app)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  string const end("[end]");
  while(in)
    {
      in.read(buf, bufsz);
      accum.append(buf, in.gcount());      
      string::size_type endpos = string::npos;
      endpos = accum.rfind(end);
      if (endpos != string::npos)
        {
          endpos += end.size();
          string tmp = accum.substr(0, endpos);
          count += extract_packets(tmp, cons, app);
          if (endpos < accum.size() - 1)
            accum = accum.substr(endpos+1);
          else
            accum.clear();
        }
    }
  return count;
}


#ifdef BUILD_UNIT_TESTS
#include "unit_tests.hh"
#include "transforms.hh"

static void 
packet_roundabout_test()
{
  string tmp;

  {
    ostringstream oss;
    packet_writer pw(oss);

    // an fdata packet
    file_data fdata(data("this is some file data"));
    file_id fid;
    calculate_ident(fdata, fid);
    pw.consume_file_data(fid, fdata);

    // an fdelta packet    
    file_data fdata2(data("this is some file data which is not the same as the first one"));
    file_id fid2;
    calculate_ident(fdata2, fid2);
    delta del;
    diff(fdata.inner(), fdata2.inner(), del);
    pw.consume_file_delta(fid, fid2, file_delta(del));

    // a rdata packet
    revision_set rev;
    rev.new_manifest = manifest_id(std::string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    split_path sp;
    file_path_internal("").split(sp);
    shared_ptr<cset> cs(new cset);
    cs->dirs_added.insert(sp);
    rev.edges.insert(std::make_pair(revision_id(std::string("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")),
                                    cs));
    revision_data rdat;
    write_revision_set(rev, rdat);
    revision_id rid;
    calculate_ident(rdat, rid);
    pw.consume_revision_data(rid, rdat);

    // a cert packet
    base64<cert_value> val;
    encode_base64(cert_value("peaches"), val);
    base64<rsa_sha1_signature> sig;
    encode_base64(rsa_sha1_signature("blah blah there is no way this is a valid signature"), sig);    
    // should be a type violation to use a file id here instead of a revision
    // id, but no-one checks...
    cert c(fid.inner(), cert_name("smell"), val, 
           rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_revision_cert(revision<cert>(c));

    keypair kp;
    // a public key packet
    encode_base64(rsa_pub_key("this is not a real rsa key"), kp.pub);
    pw.consume_public_key(rsa_keypair_id("test@lala.com"), kp.pub);

    // a keypair packet
    encode_base64(rsa_priv_key("this is not a real rsa key either!"), kp.priv);
    
    pw.consume_key_pair(rsa_keypair_id("test@lala.com"), kp);
    
    tmp = oss.str();
  }

  // read_packets needs this to convert privkeys to keypairs.
  // This doesn't test privkey packets (theres a tests/ test for that),
  // so we don't actually use the app_state for anything. So a default one
  // is ok.
  app_state aaa;
  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      packet_writer pw(oss);      
      istringstream iss(tmp);
      read_packets(iss, pw, aaa);
      BOOST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

void 
add_packet_tests(test_suite * suite)
{
  I(suite);
  suite->add(BOOST_TEST_CASE(&packet_roundabout_test));
}

#endif // BUILD_UNIT_TESTS
