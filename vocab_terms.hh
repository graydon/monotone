// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this fragment is included into both vocab.hh and vocab.cc, 
// in order to facilitate external instantiation of most of the
// vocabulary, minimize code duplication, speed up compilation, etc.

ATOMIC_NOVERIFY(external);    // "external" string in unknown system charset
ATOMIC_NOVERIFY(utf8);        // unknown string in UTF8 charset
ATOMIC(ace);                  // unknown string in ACE form

ATOMIC_NOVERIFY(id);          // hash of data
ATOMIC_NOVERIFY(data);        // meaningless blob
ATOMIC_NOVERIFY(delta);       // xdelta between 2 datas
ATOMIC_NOVERIFY(inodeprint);  // fingerprint of an inode

ATOMIC(cert_name);            // symbol-of-your-choosing
ATOMIC_NOVERIFY(cert_value);  // symbol-of-your-choosing

// some domains: "database" (+ default_server, default_pattern),
//   server_key (+ servername/key)
//   branch_alias (+ short form/long form)
//   trust_seed (+ branch/seed)
ATOMIC_NOVERIFY(var_domain);  // symbol-of-your-choosing
ATOMIC_NOVERIFY(var_name);    // symbol-of-your-choosing
ATOMIC_NOVERIFY(var_value);   // symbol-of-your-choosing

ATOMIC(rsa_keypair_id);              // keyname@domain.you.own
ATOMIC_NOVERIFY(rsa_pub_key);        // some nice numbers
ATOMIC_NOVERIFY(rsa_priv_key);       // some nice numbers
ATOMIC_NOVERIFY(rsa_sha1_signature); // some other nice numbers
ATOMIC_NOVERIFY(rsa_oaep_sha_data);

ATOMIC(netsync_session_key);  // key for netsync session HMAC
ATOMIC(netsync_hmac_value);   // 160-bit SHA-1 HMAC

DECORATE(revision);           // thing associated with a revision
DECORATE(manifest);           // thing associated with a manifest
DECORATE(file);               // thing associated with a file
DECORATE(key);               // thing associated with a key
DECORATE(epoch);              // thing associated with an epoch

ENCODING(gzip);               // thing which is gzipped
ENCODING(hexenc);             // thing which is hex-encoded
ENCODING(base64);             // thing which is base64-encoded
ENCODING(arc4);               // thing which is arc4-encrypted

ATOMIC_NOVERIFY(prefix);      // raw encoding of a merkle tree prefix
ATOMIC_NOVERIFY(merkle);      // raw encoding of a merkle tree node

// instantiate those bits of the template vocabulary actually in use.

EXTERN template class           hexenc<id>;
EXTERN template class revision< hexenc<id> >;
EXTERN template class manifest< hexenc<id> >;
EXTERN template class     file< hexenc<id> >;
EXTERN template class      key< hexenc<id> >;
EXTERN template class    epoch< hexenc<id> >;

EXTERN template class     hexenc<inodeprint>;

EXTERN template class           hexenc<data>;
EXTERN template class    epoch< hexenc<data> >;

EXTERN template class                   gzip<data>;
EXTERN template class           base64< gzip<data> >;

EXTERN template class revision< data >;
EXTERN template class manifest< data >;
EXTERN template class     file< data >;

EXTERN template class                   gzip<delta>;
EXTERN template class           base64< gzip<delta> >;

EXTERN template class manifest< delta >;
EXTERN template class     file< delta >;

EXTERN template class         arc4<rsa_priv_key>;
EXTERN template class base64< arc4<rsa_priv_key> >;
EXTERN template class base64< rsa_pub_key >;
EXTERN template class base64< rsa_sha1_signature >;
EXTERN template class hexenc< rsa_sha1_signature >;
EXTERN template class base64< cert_value >;

EXTERN template class base64< var_name >;
EXTERN template class base64< var_value >;

EXTERN template class hexenc<prefix>;
EXTERN template class base64<merkle>;
EXTERN template class base64<data>;

// instantiate those bits of the stream operator vocab (again) actually in
// use. "again" since stream operators are friends, not members.

EXTERN template std::ostream & operator<< <>(std::ostream &,           hexenc<id>   const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, revision< hexenc<id> > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, manifest< hexenc<id> > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,     file< hexenc<id> > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,    epoch< hexenc<id> > const &);

EXTERN template std::ostream & operator<< <>(std::ostream &,     hexenc<inodeprint>   const &);

EXTERN template std::ostream & operator<< <>(std::ostream &,           hexenc<data> const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,    epoch< hexenc<data> > const &);

EXTERN template std::ostream & operator<< <>(std::ostream &,                   gzip<data>     const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,           base64< gzip<data> >   const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, revision< base64< gzip<data> > > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, manifest< base64< gzip<data> > > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,     file< base64< gzip<data> > > const &);

EXTERN template std::ostream & operator<< <>(std::ostream &,                   gzip<delta>     const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,           base64< gzip<delta> >   const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, manifest< base64< gzip<delta> > > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &,     file< base64< gzip<delta> > > const &);

EXTERN template std::ostream & operator<< <>(std::ostream &,         arc4<rsa_priv_key>     const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, base64< arc4<rsa_priv_key> >   const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, base64< rsa_pub_key > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, base64< rsa_sha1_signature > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, hexenc< rsa_sha1_signature > const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, base64< cert_value > const &);

EXTERN template std::ostream & operator<< <>(std::ostream &, hexenc<prefix> const &);
EXTERN template std::ostream & operator<< <>(std::ostream &, base64<merkle> const &);
