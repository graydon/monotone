// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

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
#include "simplestring_xform.hh"
#include "keys.hh"
#include "key_store.hh"
#include "cert.hh"

using std::istream;
using std::make_pair;
using std::map;
using std::ostream;
using std::pair;
using std::string;

using boost::lexical_cast;
using boost::match_default;
using boost::match_results;
using boost::regex;
using boost::shared_ptr;

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void
packet_writer::consume_file_data(file_id const & ident,
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << ident.inner()() << "]\n"
      << trim_ws(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_file_delta(file_id const & old_id,
                                  file_id const & new_id,
                                  file_delta const & del)
{
  base64<gzip<delta> > packed;
  pack(del.inner(), packed);
  ost << "[fdelta " << old_id.inner()() << '\n'
      << "        " << new_id.inner()() << "]\n"
      << trim_ws(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_data(revision_id const & ident,
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << ident.inner()() << "]\n"
      << trim_ws(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_cert(revision<cert> const & t)
{
  ost << "[rcert " << t.inner().ident() << '\n'
      << "       " << t.inner().name() << '\n'
      << "       " << t.inner().key() << '\n'
      << "       " << trim_ws(t.inner().value()) << "]\n"
      << trim_ws(t.inner().sig()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_public_key(rsa_keypair_id const & ident,
                                  base64< rsa_pub_key > const & k)
{
  ost << "[pubkey " << ident() << "]\n"
      << trim_ws(k()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_key_pair(rsa_keypair_id const & ident,
                                keypair const & kp)
{
  ost << "[keypair " << ident() << "]\n"
      << trim_ws(kp.pub()) <<"#\n" <<trim_ws(kp.priv()) << '\n'
      << "[end]\n";
}


// -- remainder just deals with the regexes for reading packets off streams

struct
feed_packet_consumer
{
  key_store & keys;
  size_t & count;
  packet_consumer & cons;
  string ident;
  string key;
  string certname;
  string base;
  string sp;
  feed_packet_consumer(size_t & count, packet_consumer & c, key_store & keys)
   : keys(keys), count(count), cons(c),
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
  bool operator()(match_results<string::const_iterator> const & res) const
  {
    if (res.size() != 4)
      throw oops("matched impossible packet with "
                 + lexical_cast<string>(res.size()) + " matching parts: " +
                 string(res[0].first, res[0].second));
    I(res[1].matched);
    I(res[2].matched);
    I(res[3].matched);
    string type(res[1].first, res[1].second);
    string args(res[2].first, res[2].second);
    string body(res[3].first, res[3].second);
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
        match_results<string::const_iterator> matches;
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
        match_results<string::const_iterator> matches;
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
        match_results<string::const_iterator> matches;
        require(regex_match(body, matches, regex(base + "#" + base)));
        base64<rsa_pub_key> pub_dat(trim_ws(string(matches[1].first, matches[1].second)));
        base64<rsa_priv_key> priv_dat(trim_ws(string(matches[2].first, matches[2].second)));
        cons.consume_key_pair(rsa_keypair_id(args), keypair(pub_dat, priv_dat));
      }
    else if (type == "privkey")
      {
        L(FL("read pubkey data packet"));
        require(regex_match(args, regex(key)));
        require(regex_match(body, regex(base)));
        string contents(trim_ws(body));
        keypair kp;
        migrate_private_key(keys,
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
extract_packets(string const & s, packet_consumer & cons, key_store & keys)
{
  static string const head("\\[([a-z]+)[[:space:]]+([^\\[\\]]+)\\]");
  static string const body("([^\\[\\]]+)");
  static string const tail("\\[end\\]");
  static string const whole = head + body + tail;
  regex expr(whole);
  size_t count = 0;
  regex_grep(feed_packet_consumer(count, cons, keys), s, expr, match_default);
  return count;
}


size_t
read_packets(istream & in, packet_consumer & cons, key_store & keys)
{
  string accum, tmp;
  size_t count = 0;
  size_t const bufsz = 0xff;
  char buf[bufsz];
  static string const end("[end]");
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
          count += extract_packets(tmp, cons, keys);
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

using std::istringstream;
using std::ostringstream;

UNIT_TEST(packet, roundabout)
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
    revision_t rev;
    rev.new_manifest = manifest_id(string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    split_path sp;
    file_path_internal("").split(sp);
    shared_ptr<cset> cs(new cset);
    cs->dirs_added.insert(sp);
    rev.edges.insert(make_pair(revision_id(string("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")),
                                    cs));
    revision_data rdat;
    write_revision(rev, rdat);
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
      read_packets(iss, pw, aaa.keys);
      UNIT_TEST_CHECK(oss.str() == tmp);
      tmp = oss.str();
    }
}

#endif // BUILD_UNIT_TESTS

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
