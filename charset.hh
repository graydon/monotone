#ifndef __CHARSET_HH__
#define __CHARSET_HH__

#include "vocab.hh"

// charset conversions
void charset_convert(std::string const & src_charset, std::string const & dst_charset,
                     std::string const & src, std::string & dst);
void system_to_utf8(external const & system, utf8 & utf);
void utf8_to_system(utf8 const & utf, external & system);
void utf8_to_system(utf8 const & utf, std::string & system);
void ace_to_utf8(ace const & ac, utf8 & utf);
void utf8_to_ace(utf8 const & utf, ace & a);
bool utf8_validate(utf8 const & utf);

// returns length in characters (not bytes)
size_t display_width(utf8 const & utf);

// specific internal / external conversions for various vocab terms
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

#endif
