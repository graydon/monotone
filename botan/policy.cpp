/*************************************************
* Default Policy Source File                     *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#include <botan/look_add.h>
#include <botan/conf.h>
#include <botan/init.h>
#include <botan/oids.h>

namespace Botan {

namespace {

/*************************************************
* OID loading helper function                    *
*************************************************/
void add_oid(const std::string& oid_str, const std::string& name)
   {
   OIDS::add_oid(OID(oid_str), name);
   }

}

/*************************************************
* Load all of the default OIDs                   *
*************************************************/
void add_default_oids()
   {
   add_oid("1.2.840.113549.1.1.1", "RSA");
   add_oid("2.5.8.1.1", "RSA");
   add_oid("1.2.840.10040.4.1", "DSA");
   add_oid("1.2.840.10046.2.1", "DH");
   add_oid("1.3.6.1.4.1.3029.1.2.1", "ELG");

   add_oid("1.3.14.3.2.7", "DES/CBC");
   add_oid("1.2.840.113549.3.7", "TripleDES/CBC");
   add_oid("1.2.840.113549.3.2", "RC2/CBC");
   add_oid("1.2.840.113533.7.66.10", "CAST-128/CBC");
   add_oid("2.16.840.1.101.3.4.1.2", "AES-128/CBC");
   add_oid("2.16.840.1.101.3.4.1.22", "AES-192/CBC");
   add_oid("2.16.840.1.101.3.4.1.42", "AES-256/CBC");

   add_oid("1.2.840.113549.2.5", "MD5");
   add_oid("1.3.14.3.2.26", "SHA-160");

   add_oid("1.2.840.113549.1.9.16.3.6", "KeyWrap.TripleDES");
   add_oid("1.2.840.113549.1.9.16.3.7", "KeyWrap.RC2");
   add_oid("1.2.840.113533.7.66.15", "KeyWrap.CAST-128");
   add_oid("2.16.840.1.101.3.4.1.5", "KeyWrap.AES-128");
   add_oid("2.16.840.1.101.3.4.1.25", "KeyWrap.AES-192");
   add_oid("2.16.840.1.101.3.4.1.45", "KeyWrap.AES-256");

   add_oid("1.2.840.113549.1.9.16.3.8", "Compression.Zlib");

   add_oid("1.2.840.113549.1.1.1", "RSA/EME-PKCS1-v1_5");
   add_oid("1.2.840.113549.1.1.2", "RSA/EMSA3(MD2)");
   add_oid("1.2.840.113549.1.1.4", "RSA/EMSA3(MD5)");
   add_oid("1.2.840.113549.1.1.5", "RSA/EMSA3(SHA-160)");
   add_oid("1.2.840.113549.1.1.11", "RSA/EMSA3(SHA-256)");
   add_oid("1.2.840.113549.1.1.12", "RSA/EMSA3(SHA-384)");
   add_oid("1.2.840.113549.1.1.13", "RSA/EMSA3(SHA-512)");
   add_oid("1.3.36.3.3.1.2", "RSA/EMSA3(RIPEMD-160)");
   add_oid("1.2.840.10040.4.3", "DSA/EMSA1(SHA-160)");

   add_oid("2.5.4.3",  "X520.CommonName");
   add_oid("2.5.4.4",  "X520.Surname");
   add_oid("2.5.4.5",  "X520.SerialNumber");
   add_oid("2.5.4.6",  "X520.Country");
   add_oid("2.5.4.7",  "X520.Locality");
   add_oid("2.5.4.8",  "X520.State");
   add_oid("2.5.4.10", "X520.Organization");
   add_oid("2.5.4.11", "X520.OrganizationalUnit");
   add_oid("2.5.4.12", "X520.Title");
   add_oid("2.5.4.42", "X520.GivenName");
   add_oid("2.5.4.43", "X520.Initials");
   add_oid("2.5.4.44", "X520.GenerationalQualifier");
   add_oid("2.5.4.46", "X520.DNQualifier");
   add_oid("2.5.4.65", "X520.Pseudonym");

   add_oid("1.2.840.113549.1.5.12", "PKCS5.PBKDF2");
   add_oid("1.2.840.113549.1.5.1",  "PBE-PKCS5v15(MD2,DES/CBC)");
   add_oid("1.2.840.113549.1.5.4",  "PBE-PKCS5v15(MD2,RC2/CBC)");
   add_oid("1.2.840.113549.1.5.3",  "PBE-PKCS5v15(MD5,DES/CBC)");
   add_oid("1.2.840.113549.1.5.6",  "PBE-PKCS5v15(MD5,RC2/CBC)");
   add_oid("1.2.840.113549.1.5.10", "PBE-PKCS5v15(SHA-160,DES/CBC)");
   add_oid("1.2.840.113549.1.5.11", "PBE-PKCS5v15(SHA-160,RC2/CBC)");
   add_oid("1.2.840.113549.1.5.13", "PBE-PKCS5v20");

   add_oid("1.2.840.113549.1.9.1", "PKCS9.EmailAddress");
   add_oid("1.2.840.113549.1.9.2", "PKCS9.UnstructuredName");
   add_oid("1.2.840.113549.1.9.3", "PKCS9.ContentType");
   add_oid("1.2.840.113549.1.9.4", "PKCS9.MessageDigest");
   add_oid("1.2.840.113549.1.9.7", "PKCS9.ChallengePassword");
   add_oid("1.2.840.113549.1.9.14", "PKCS9.ExtensionRequest");

   add_oid("1.2.840.113549.1.7.1",      "CMS.DataContent");
   add_oid("1.2.840.113549.1.7.2",      "CMS.SignedData");
   add_oid("1.2.840.113549.1.7.3",      "CMS.EnvelopedData");
   add_oid("1.2.840.113549.1.7.5",      "CMS.DigestedData");
   add_oid("1.2.840.113549.1.7.6",      "CMS.EncryptedData");
   add_oid("1.2.840.113549.1.9.16.1.2", "CMS.AuthenticatedData");
   add_oid("1.2.840.113549.1.9.16.1.9", "CMS.CompressedData");

   add_oid("2.5.29.14", "X509v3.SubjectKeyIdentifier");
   add_oid("2.5.29.15", "X509v3.KeyUsage");
   add_oid("2.5.29.17", "X509v3.SubjectAlternativeName");
   add_oid("2.5.29.18", "X509v3.IssuerAlternativeName");
   add_oid("2.5.29.19", "X509v3.BasicConstraints");
   add_oid("2.5.29.20", "X509v3.CRLNumber");
   add_oid("2.5.29.21", "X509v3.ReasonCode");
   add_oid("2.5.29.23", "X509v3.HoldInstructionCode");
   add_oid("2.5.29.24", "X509v3.InvalidityDate");
   add_oid("2.5.29.32", "X509v3.CertificatePolicies");
   add_oid("2.5.29.35", "X509v3.AuthorityKeyIdentifier");
   add_oid("2.5.29.36", "X509v3.PolicyConstraints");
   add_oid("2.5.29.37", "X509v3.ExtendedKeyUsage");

   add_oid("2.5.29.32.0", "X509v3.AnyPolicy");

   add_oid("1.3.6.1.5.5.7.3.1", "PKIX.ServerAuth");
   add_oid("1.3.6.1.5.5.7.3.2", "PKIX.ClientAuth");
   add_oid("1.3.6.1.5.5.7.3.3", "PKIX.CodeSigning");
   add_oid("1.3.6.1.5.5.7.3.4", "PKIX.EmailProtection");
   add_oid("1.3.6.1.5.5.7.3.5", "PKIX.IPsecEndSystem");
   add_oid("1.3.6.1.5.5.7.3.6", "PKIX.IPsecTunnel");
   add_oid("1.3.6.1.5.5.7.3.7", "PKIX.IPsecUser");
   add_oid("1.3.6.1.5.5.7.3.8", "PKIX.TimeStamping");
   add_oid("1.3.6.1.5.5.7.3.9", "PKIX.OCSPSigning");
   }

/*************************************************
* Load the list of default aliases               *
*************************************************/
void add_default_aliases()
   {
   add_alias("OpenPGP.Cipher.1",  "IDEA");
   add_alias("OpenPGP.Cipher.2",  "TripleDES");
   add_alias("OpenPGP.Cipher.3",  "CAST-128");
   add_alias("OpenPGP.Cipher.4",  "Blowfish");
   add_alias("OpenPGP.Cipher.5",  "SAFER-SK(13)");
   add_alias("OpenPGP.Cipher.7",  "AES-128");
   add_alias("OpenPGP.Cipher.8",  "AES-192");
   add_alias("OpenPGP.Cipher.9",  "AES-256");
   add_alias("OpenPGP.Cipher.10", "Twofish");

   add_alias("OpenPGP.Digest.1", "MD5");
   add_alias("OpenPGP.Digest.2", "SHA-1");
   add_alias("OpenPGP.Digest.3", "RIPEMD-160");
   add_alias("OpenPGP.Digest.5", "MD2");
   add_alias("OpenPGP.Digest.6", "Tiger(24,3)");
   add_alias("OpenPGP.Digest.7", "HAVAL(20,5)");
   add_alias("OpenPGP.Digest.8", "SHA-256");

   add_alias("TLS.Digest.0",     "Parallel(MD5,SHA-1)");

   add_alias("EME-PKCS1-v1_5",  "PKCS1v15");
   add_alias("OAEP-MGF1",       "EME1");
   add_alias("EME-OAEP",        "EME1");
   add_alias("X9.31",           "EMSA2");
   add_alias("EMSA-PKCS1-v1_5", "EMSA3");
   add_alias("PSS-MGF1",        "EMSA4");
   add_alias("EMSA-PSS",        "EMSA4");

   add_alias("Rijndael", "AES");
   add_alias("3DES",     "TripleDES");
   add_alias("DES-EDE",  "TripleDES");
   add_alias("CAST5",    "CAST-128");
   add_alias("SHA1",     "SHA-160");
   add_alias("SHA-1",    "SHA-160");
   add_alias("SEAL",     "SEAL-3.0-BE");
   add_alias("MARK-4",   "ARC4(256)");
   }

namespace Init {

/*************************************************
* Set the default options                        *
*************************************************/
void set_default_options()
   {
   Config::set("base/memory_chunk", "32*1024");
   Config::set("base/default_pbe", "PBE-PKCS5v20(SHA-1,TripleDES/CBC)");
   Config::set("base/pkcs8_tries", "3");

   Config::set("pk/blinder_size", "64");
   Config::set("pk/test/public", "basic");
   Config::set("pk/test/private", "basic");
   Config::set("pk/test/private_gen", "all");

   Config::set("pem/search", "4*1024");
   Config::set("pem/forgive", "8");
   Config::set("pem/width", "64");

   Config::set("rng/min_entropy", "384", false);
   Config::set("rng/es_files", "/dev/urandom:/dev/random");
   Config::set("rng/egd_path", "/var/run/egd-pool:/dev/egd-pool");
   Config::set("rng/ms_capi_prov_type", "INTEL_SEC:RSA_FULL");
   Config::set("rng/unix_path", "/usr/ucb:/usr/etc:/etc");

   Config::set("x509/validity_slack", "24h");
   Config::set("x509/v1_assume_ca", "false");
   Config::set("x509/cache_verify_results", "30m");

   Config::set("x509/ca/allow_ca", "false");
   Config::set("x509/ca/basic_constraints", "always");
   Config::set("x509/ca/default_expire", "1y");
   Config::set("x509/ca/signing_offset", "30s");
   Config::set("x509/ca/rsa_hash", "SHA-1");
   Config::set("x509/ca/str_type", "latin1");

   Config::set("x509/crl/unknown_critical", "ignore");
   Config::set("x509/crl/next_update", "7d");

   Config::set("x509/exts/basic_constraints", "critical");
   Config::set("x509/exts/subject_key_id", "yes");
   Config::set("x509/exts/authority_key_id", "yes");
   Config::set("x509/exts/subject_alternative_name", "yes");
   Config::set("x509/exts/issuer_alternative_name", "yes");
   Config::set("x509/exts/key_usage", "critical");
   Config::set("x509/exts/extended_key_usage", "yes");
   Config::set("x509/exts/crl_number", "yes");
   }

}

}
