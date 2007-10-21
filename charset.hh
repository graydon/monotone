#ifndef __CHARSET_HH__
#define __CHARSET_HH__

// Copyright (C) 2002 Graydon Hoare <graydon@pobox.com>
//
// This program is made available under the GNU GPL version 2.0 or
// greater. See the accompanying file COPYING for details.
//
// This program is distributed WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE.

#include "vocab.hh"

// Charset conversions.

void charset_convert(std::string const & src_charset,
                     std::string const & dst_charset,
                     std::string const & src,
                     std::string & dst,
                     bool best_effort);
void system_to_utf8(external const & system, utf8 & utf);
void utf8_to_system_strict(utf8 const & utf, external & system);
void utf8_to_system_strict(utf8 const & utf, std::string & system);
void utf8_to_system_best_effort(utf8 const & utf, external & system);
void utf8_to_system_best_effort(utf8 const & utf, std::string & system);
bool utf8_validate(utf8 const & utf);

// Returns length in characters (not bytes).
// Is not aware of combining and invisible characters.
size_t display_width(utf8 const & utf);

// Specific internal / external conversions for various vocab terms.
void internalize_cert_name(utf8 const & utf, cert_name & c);
void internalize_cert_name(external const & ext, cert_name & c);
void externalize_cert_name(cert_name const & c, utf8 & utf);
void externalize_cert_name(cert_name const & c, external & ext);
void internalize_rsa_keypair_id(utf8 const & utf, rsa_keypair_id & key);
void internalize_rsa_keypair_id(external const & ext, rsa_keypair_id & key);
void externalize_rsa_keypair_id(rsa_keypair_id const & key, utf8 & utf);
void externalize_rsa_keypair_id(rsa_keypair_id const & key, external & ext);
void internalize_var_domain(utf8 const & utf, var_domain & d);
void internalize_var_domain(external const & ext, var_domain & d);
void externalize_var_domain(var_domain const & d, utf8 & utf);
void externalize_var_domain(var_domain const & d, external & ext);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
