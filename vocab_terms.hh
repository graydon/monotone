// copyright (C) 2002, 2003 graydon hoare <graydon@pobox.com>
// all rights reserved.
// licensed to the public under the terms of the GNU GPL (>= 2)
// see the file COPYING for details

// this fragment is included into both vocab.hh and vocab.cc, 
// in order to facilitate external instantiation of most of the
// vocabulary, minimize code duplication, speed up compilation, etc.

ATOMIC(external);             // "external" string in unknown system charset
ATOMIC(utf8);                 // unknown string in UTF8 charset
ATOMIC(ace);                  // unknown string in ACE form

ATOMIC(id);                   // hash of data
ATOMIC(data);                 // meaningless blob
ATOMIC(delta);                // xdelta between 2 datas

ATOMIC(local_path);           // non-absolute file
ATOMIC(file_path);            // non-absolute, non-bookeeping file

ATOMIC(cert_name);            // symbol-of-your-choosing
ATOMIC(cert_value);           // symbol-of-your-choosing

// some domains: "database" (+ default_server, default_collection),
//   server_key (+ servername/key)
//   branch_alias (+ short form/long form)
//   trust_seed (+ branch/seed)
ATOMIC(var_domain);           // symbol-of-your-choosing
ATOMIC(var_name);             // symbol-of-your-choosing
ATOMIC(var_value);            // symbol-of-your-choosing

ATOMIC(rsa_keypair_id);       // keyname@domain.you.own
ATOMIC(rsa_pub_key);          // some nice numbers
ATOMIC(rsa_priv_key);         // some nice numbers
ATOMIC(rsa_sha1_signature);   // some other nice numbers

DECORATE(revision);           // thing associated with a revision
DECORATE(manifest);           // thing associated with a manifest
DECORATE(file);               // thing associated with a file
DECORATE(epoch);              // thing associated with an epoch

ENCODING(gzip);               // thing which is gzipped
ENCODING(hexenc);             // thing which is hex-encoded
ENCODING(base64);             // thing which is base64-encoded
ENCODING(arc4);               // thing which is arc4-encrypted

ATOMIC(prefix);               // raw encoding of a merkle tree prefix
ATOMIC(merkle);               // raw encoding of a merkle tree node

// instantiate those bits of the template vocabulary actually in use.

EXTERN template class           hexenc<id>;
EXTERN template class revision< hexenc<id> >;
EXTERN template class manifest< hexenc<id> >;
EXTERN template class     file< hexenc<id> >;
EXTERN template class    epoch< hexenc<id> >;

EXTERN template class           hexenc<data>;
EXTERN template class    epoch< hexenc<data> >;

EXTERN template class                   gzip<data>;
EXTERN template class           base64< gzip<data> >;
EXTERN template class revision< base64< gzip<data> > >;
EXTERN template class manifest< base64< gzip<data> > >;
EXTERN template class     file< base64< gzip<data> > >;

EXTERN template class                   gzip<delta>;
EXTERN template class           base64< gzip<delta> >;
EXTERN template class manifest< base64< gzip<delta> > >;
EXTERN template class     file< base64< gzip<delta> > >;

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
