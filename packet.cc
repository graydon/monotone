// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "base.hh"
#include <sstream>

#include "cset.hh"
#include "constants.hh"
#include "packet.hh"
#include "revision.hh"
#include "sanity.hh"
#include "transforms.hh"
#include "simplestring_xform.hh"
#include "cert.hh"
#include "key_store.hh" // for keypair
#include "char_classifiers.hh"

using std::istream;
using std::istringstream;
using std::make_pair;
using std::map;
using std::ostream;
using std::pair;
using std::string;

using boost::shared_ptr;

// --- packet writer ---

packet_writer::packet_writer(ostream & o) : ost(o) {}

void
packet_writer::consume_file_data(file_id const & ident,
                                 file_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[fdata " << encode_hexenc(ident.inner()()) << "]\n"
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
  ost << "[fdelta " << encode_hexenc(old_id.inner()()) << '\n'
      << "        " << encode_hexenc(new_id.inner()()) << "]\n"
      << trim_ws(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_data(revision_id const & ident,
                                     revision_data const & dat)
{
  base64<gzip<data> > packed;
  pack(dat.inner(), packed);
  ost << "[rdata " << encode_hexenc(ident.inner()()) << "]\n"
      << trim_ws(packed()) << '\n'
      << "[end]\n";
}

void
packet_writer::consume_revision_cert(revision<cert> const & t)
{
  ost << "[rcert " << encode_hexenc(t.inner().ident.inner()()) << '\n'
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

void
packet_writer::consume_old_private_key(rsa_keypair_id const & ident,
                                       base64<old_arc4_rsa_priv_key> const & k)
{
  ost << "[privkey " << ident() << "]\n"
      << trim_ws(k()) << '\n'
      << "[end]\n";
}


// --- reading packets from streams ---
namespace
{
  struct
  feed_packet_consumer
  {
    size_t & count;
    packet_consumer & cons;
    feed_packet_consumer(size_t & count, packet_consumer & c)
      : count(count), cons(c)
    {}
    void validate_id(string const & id) const
    {
      E(id.size() == constants::idlen
        && id.find_first_not_of(constants::legal_id_bytes) == string::npos,
        F("malformed packet: invalid identifier"));
    }
    void validate_base64(string const & s) const
    {
      E(s.size() > 0
        && s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        F("malformed packet: invalid base64 block"));
    }
    void validate_arg_base64(string const & s) const
    {
      E(s.find_first_not_of(constants::legal_base64_bytes) == string::npos,
        F("malformed packet: invalid base64 block"));
    }
    void validate_key(string const & k) const
    {
      E(k.size() > 0
        && k.find_first_not_of(constants::legal_key_name_bytes) == string::npos,
        F("malformed packet: invalid key name"));
    }
    void validate_certname(string const & cn) const
    {
      E(cn.size() > 0
        && cn.find_first_not_of(constants::legal_cert_name_bytes) == string::npos,
        F("malformed packet: invalid cert name"));
    }
    void validate_no_more_args(istringstream & iss) const
    {
      string next;
      iss >> next;
      E(next.size() == 0,
        F("malformed packet: too many arguments in header"));
    }

    void data_packet(string const & args, string const & body,
                     bool is_revision) const
    {
      L(FL("read %s data packet") % (is_revision ? "revision" : "file"));
      validate_id(args);
      validate_base64(body);

      id hash(decode_hexenc(args));
      data contents;
      unpack(base64<gzip<data> >(body), contents);
      if (is_revision)
        cons.consume_revision_data(revision_id(hash),
                                   revision_data(contents));
      else
        cons.consume_file_data(file_id(hash),
                               file_data(contents));
    }

    void fdelta_packet(string const & args, string const & body) const
    {
      L(FL("read delta packet"));
      istringstream iss(args);
      string src_id; iss >> src_id; validate_id(src_id);
      string dst_id; iss >> dst_id; validate_id(dst_id);
      validate_no_more_args(iss);
      validate_base64(body);

      id src_hash(decode_hexenc(src_id)),
         dst_hash(decode_hexenc(dst_id));
      delta contents;
      unpack(base64<gzip<delta> >(body), contents);
      cons.consume_file_delta(file_id(src_hash),
                              file_id(dst_hash),
                              file_delta(contents));
    }
    static void read_rest(istream& in, string& dest)
    {
    
      while( true )
        {
          string t;
          in >> t;
          if( t.size() == 0 ) break;
          dest += t;
        }
    }
    void rcert_packet(string const & args, string const & body) const
    {
      L(FL("read cert packet"));
      istringstream iss(args);
      string certid; iss >> certid; validate_id(certid);
      string name;   iss >> name;   validate_certname(name);
      string keyid;  iss >> keyid;  validate_key(keyid);
      string val;    
      read_rest(iss,val);           validate_arg_base64(val);    

      revision_id hash(decode_hexenc(certid));
      validate_base64(body);
      // canonicalize the base64 encodings to permit searches
      cert t = cert(hash,
                    cert_name(name),
                    base64<cert_value>(canonical_base64(val)),
                    rsa_keypair_id(keyid),
                    base64<rsa_sha1_signature>(canonical_base64(body)));
      cons.consume_revision_cert(revision<cert>(t));
    }

    void pubkey_packet(string const & args, string const & body) const
    {
      L(FL("read pubkey packet"));
      validate_key(args);
      validate_base64(body);

      cons.consume_public_key(rsa_keypair_id(args),
                              base64<rsa_pub_key>(body));
    }

    void keypair_packet(string const & args, string const & body) const
    {
      L(FL("read keypair packet"));
      string::size_type hashpos = body.find('#');
      string pub(body, 0, hashpos);
      string priv(body, hashpos+1);

      validate_key(args);
      validate_base64(pub);
      validate_base64(priv);
      cons.consume_key_pair(rsa_keypair_id(args),
                            keypair(base64<rsa_pub_key>(pub),
                                    base64<rsa_priv_key>(priv)));
    }

    void privkey_packet(string const & args, string const & body) const
    {
      L(FL("read privkey packet"));
      validate_key(args);
      validate_base64(body);
      cons.consume_old_private_key(rsa_keypair_id(args),
                                   base64<old_arc4_rsa_priv_key>(body));
    }
  
    void operator()(string const & type,
                    string const & args,
                    string const & body) const
    {
      if (type == "rdata")
        data_packet(args, body, true);
      else if (type == "fdata")
        data_packet(args, body, false);
      else if (type == "fdelta")
        fdelta_packet(args, body);
      else if (type == "rcert")
        rcert_packet(args, body);
      else if (type == "pubkey")
        pubkey_packet(args, body);
      else if (type == "keypair")
        keypair_packet(args, body);
      else if (type == "privkey")
        privkey_packet(args, body);
      else
        {
          W(F("unknown packet type: '%s'") % type);
          return;
        }
      ++count;
    }
  };
} // anonymous namespace

static size_t
extract_packets(string const & s, packet_consumer & cons)
{
  size_t count = 0;
  feed_packet_consumer feeder(count, cons);

  string::const_iterator p, tbeg, tend, abeg, aend, bbeg, bend;

  enum extract_state {
    skipping, open_bracket, scanning_type, found_type,
    scanning_args, found_args, scanning_body,
    end_1, end_2, end_3, end_4, end_5
  } state = skipping;
  
  for (p = s.begin(); p != s.end(); p++)
    switch (state)
      {
      case skipping: if (*p == '[') state = open_bracket; break;
      case open_bracket:
        if (is_alpha (*p))
          state = scanning_type;
        else
          state = skipping;
        tbeg = p;
        break;
      case scanning_type:
        if (!is_alpha (*p))
          {
            state = is_space(*p) ? found_type : skipping;
            tend = p;
          }
        break;
      case found_type:
        if (!is_space (*p))
          {
            state = (*p != ']') ? scanning_args : skipping;
            abeg = p;
          }
        break;
      case scanning_args:
        if (*p == ']')
          {
            state = found_args;
            aend = p;
          }
        break;
      case found_args:
        state = (*p != '[' && *p != ']') ? scanning_body : skipping;
        bbeg = p;
        break;
      case scanning_body:
        if (*p == '[')
          {
            state = end_1;
            bend = p;
          }
        else if (*p == ']')
          state = skipping;
        break;

      case end_1: state = (*p == 'e') ? end_2 : skipping; break;
      case end_2: state = (*p == 'n') ? end_3 : skipping; break;
      case end_3: state = (*p == 'd') ? end_4 : skipping; break;
      case end_4:
        if (*p == ']')
          feeder(string(tbeg, tend), string(abeg, aend), string(bbeg, bend));
        state = skipping;
        break;
      default:
        I(false);
      }
  return count;
}

size_t
read_packets(istream & in, packet_consumer & cons)
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
          count += extract_packets(tmp, cons);
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
#include "xdelta.hh"

using std::ostringstream;

UNIT_TEST(packet, validators)
{
  ostringstream oss;
  packet_writer pw(oss);
  size_t count;
  feed_packet_consumer f(count, pw);

#define N_THROW(expr) UNIT_TEST_CHECK_NOT_THROW(expr, informative_failure)
#define Y_THROW(expr) UNIT_TEST_CHECK_THROW(expr, informative_failure)

  // validate_id
  N_THROW(f.validate_id("5d7005fadff386039a8d066684d22d369c1e6c94"));
  Y_THROW(f.validate_id(""));
  Y_THROW(f.validate_id("5d7005fadff386039a8d066684d22d369c1e6c9"));
  for (int i = 1; i < std::numeric_limits<unsigned char>::max(); i++)
    if (!((i >= '0' && i <= '9')
          || (i >= 'a' && i <= 'f')))
      Y_THROW(f.validate_id(string("5d7005fadff386039a8d066684d22d369c1e6c9")
                            + char(i)));

  // validate_base64
  N_THROW(f.validate_base64("YmwK"));
  N_THROW(f.validate_base64(" Y m x h a A o = "));
  N_THROW(f.validate_base64("ABCD EFGH IJKL MNOP QRST UVWX YZ"
                            "abcd efgh ijkl mnop qrst uvwx yz"
                            "0123 4567 89/+ z\t=\r=\n="));

  Y_THROW(f.validate_base64(""));
  Y_THROW(f.validate_base64("!@#$"));

  // validate_key
  N_THROW(f.validate_key("graydon@venge.net"));
  N_THROW(f.validate_key("dscherger+mtn"));
  N_THROW(f.validate_key("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789-.@+_"));
  Y_THROW(f.validate_key(""));
  Y_THROW(f.validate_key("graydon at venge dot net"));

  // validate_certname
  N_THROW(f.validate_certname("graydon-at-venge-dot-net"));
  N_THROW(f.validate_certname("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                              "abcdefghijklmnopqrstuvwxyz"
                              "0123456789-"));
    
  Y_THROW(f.validate_certname(""));
  Y_THROW(f.validate_certname("graydon@venge.net"));
  Y_THROW(f.validate_certname("graydon at venge dot net"));

  // validate_no_more_args
  {
    istringstream iss("a b");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    string b; iss >> b; UNIT_TEST_CHECK(b == "b");
    N_THROW(f.validate_no_more_args(iss));
  }
  {
    istringstream iss("a ");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    N_THROW(f.validate_no_more_args(iss));
  }
  {
    istringstream iss("a b");
    string a; iss >> a; UNIT_TEST_CHECK(a == "a");
    Y_THROW(f.validate_no_more_args(iss));
  }
}

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
    rev.new_manifest = manifest_id(decode_hexenc(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    shared_ptr<cset> cs(new cset);
    cs->dirs_added.insert(file_path_internal(""));
    rev.edges.insert(make_pair(revision_id(decode_hexenc(
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")), cs));
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

    // cert now accepts revision_id exclusively, so we need to cast the
    // file_id to create a cert to test the packet writer with.
    cert c(revision_id(fid.inner()()), cert_name("smell"), val,
           rsa_keypair_id("fun@moonman.com"), sig);
    pw.consume_revision_cert(revision<cert>(c));

    keypair kp;
    // a public key packet
    encode_base64(rsa_pub_key("this is not a real rsa key"), kp.pub);
    pw.consume_public_key(rsa_keypair_id("test@lala.com"), kp.pub);

    // a keypair packet
    encode_base64(rsa_priv_key("this is not a real rsa key either!"), kp.priv);
    pw.consume_key_pair(rsa_keypair_id("test@lala.com"), kp);

    // an old privkey packet
    base64<old_arc4_rsa_priv_key> oldpriv;
    encode_base64(old_arc4_rsa_priv_key("and neither is this!"), oldpriv);
    pw.consume_old_private_key(rsa_keypair_id("test@lala.com"), oldpriv);

    tmp = oss.str();
  }

  for (int i = 0; i < 10; ++i)
    {
      // now spin around sending and receiving this a few times
      ostringstream oss;
      packet_writer pw(oss);
      istringstream iss(tmp);
      read_packets(iss, pw);
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
