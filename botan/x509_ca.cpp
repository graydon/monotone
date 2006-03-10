/*************************************************
* X.509 Certificate Authority Source File        *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/x509_ca.h>
#include <botan/x509stor.h>
#include <botan/conf.h>
#include <botan/lookup.h>
#include <botan/look_pk.h>
#include <botan/numthry.h>
#include <botan/oids.h>
#include <botan/util.h>
#include <memory>
#include <set>

namespace Botan {

namespace {

/*************************************************
* Load the certificate and private key           *
*************************************************/
MemoryVector<byte> make_SKID(const MemoryRegion<byte>& pub_key)
   {
   std::auto_ptr<HashFunction> hash(get_hash("SHA-1"));
   return hash->process(pub_key);
   }

}

/*************************************************
* Load the certificate and private key           *
*************************************************/
X509_CA::X509_CA(const X509_Certificate& c,
                 const PKCS8_PrivateKey& key) : cert(c)
   {
   const PKCS8_PrivateKey* key_pointer = &key;
   if(!dynamic_cast<const PK_Signing_Key*>(key_pointer))
      throw Invalid_Argument("X509_CA: " + key.algo_name() + " cannot sign");

   if(!cert.is_CA_cert())
      throw Invalid_Argument("X509_CA: This certificate is not for a CA");

   std::string padding;
   Signature_Format format;

   Config::choose_sig_format(key.algo_name(), padding, format);

   ca_sig_algo.oid = OIDS::lookup(key.algo_name() + "/" + padding);
   ca_sig_algo.parameters = key.DER_encode_params();

   const PK_Signing_Key& sig_key = dynamic_cast<const PK_Signing_Key&>(key);
   signer = get_pk_signer(sig_key, padding, format);
   }

/*************************************************
* Sign a PKCS #10 certificate request            *
*************************************************/
X509_Certificate X509_CA::sign_request(const PKCS10_Request& req,
                                       u32bit expire_time) const
   {
   if(req.is_CA() && !Config::get_bool("x509/ca/allow_ca"))
      throw Policy_Violation("X509_CA: Attempted to sign new CA certificate");

   Key_Constraints constraints;
   if(req.is_CA())
      constraints = Key_Constraints(KEY_CERT_SIGN | CRL_SIGN);
   else
      {
      std::auto_ptr<X509_PublicKey> key(req.subject_public_key());
      constraints = X509::find_constraints(*key, req.constraints());
      }

   if(expire_time == 0)
      expire_time = Config::get_time("x509/ca/default_expire");

   const u64bit current_time = system_time();

   X509_Time not_before(current_time);
   X509_Time not_after(current_time + expire_time);

   return make_cert(signer, ca_sig_algo, req.raw_public_key(),
                    cert.subject_key_id(), not_before, not_after,
                    cert.subject_dn(), req.subject_dn(),
                    req.is_CA(), req.path_limit(), req.subject_alt_name(),
                    constraints, req.ex_constraints());
   }

/*************************************************
* Create a new certificate                       *
*************************************************/
X509_Certificate X509_CA::make_cert(PK_Signer* signer,
                                    const AlgorithmIdentifier& sig_algo,
                                    const MemoryRegion<byte>& pub_key,
                                    const MemoryRegion<byte>& auth_key_id,
                                    const X509_Time& not_before,
                                    const X509_Time& not_after,
                                    const X509_DN& issuer_dn,
                                    const X509_DN& subject_dn,
                                    bool is_CA, u32bit path_limit,
                                    const AlternativeName& subject_alt,
                                    Key_Constraints constraints,
                                    const std::vector<OID>& ex_constraints)
   {
   const u32bit X509_CERT_VERSION = 2;
   const u32bit SERIAL_BITS = 128;

   DER_Encoder tbs_cert;

   tbs_cert.start_sequence();
   tbs_cert.start_explicit(ASN1_Tag(0));
   DER::encode(tbs_cert, X509_CERT_VERSION);
   tbs_cert.end_explicit(ASN1_Tag(0));

   DER::encode(tbs_cert, random_integer(SERIAL_BITS, Nonce));
   DER::encode(tbs_cert, sig_algo);
   DER::encode(tbs_cert, issuer_dn);
   tbs_cert.start_sequence();
   DER::encode(tbs_cert, not_before);
   DER::encode(tbs_cert, not_after);
   tbs_cert.end_sequence();
   DER::encode(tbs_cert, subject_dn);
   tbs_cert.add_raw_octets(pub_key);

   tbs_cert.start_explicit(ASN1_Tag(3));
   tbs_cert.start_sequence();

   DER_Encoder v3_ext;

   DER::encode(v3_ext, make_SKID(pub_key), OCTET_STRING);
   do_ext(tbs_cert, v3_ext, "X509v3.SubjectKeyIdentifier", "subject_key_id");

   if(auth_key_id.size())
      {
      v3_ext.start_sequence();
      DER::encode(v3_ext, auth_key_id, OCTET_STRING,
                  ASN1_Tag(0), CONTEXT_SPECIFIC);
      v3_ext.end_sequence();
      do_ext(tbs_cert, v3_ext, "X509v3.AuthorityKeyIdentifier",
             "authority_key_id");
      }

   if(is_CA || (Config::get_string("x509/ca/basic_constraints") == "always"))
      {
      v3_ext.start_sequence();
      if(is_CA)
         {
         DER::encode(v3_ext, true);
         if(path_limit != NO_CERT_PATH_LIMIT)
            DER::encode(v3_ext, path_limit);
         }
      v3_ext.end_sequence();
      do_ext(tbs_cert, v3_ext, "X509v3.BasicConstraints", "basic_constraints");
      }

   if(subject_alt.has_items())
      {
      DER::encode(v3_ext, subject_alt);
      do_ext(tbs_cert, v3_ext, "X509v3.SubjectAlternativeName",
             "subject_alternative_name");
      }

   if(constraints != NO_CONSTRAINTS)
      {
      DER::encode(v3_ext, constraints);
      do_ext(tbs_cert, v3_ext, "X509v3.KeyUsage", "key_usage");
      }

   if(ex_constraints.size())
      {
      v3_ext.start_sequence();
      for(u32bit j = 0; j != ex_constraints.size(); j++)
         DER::encode(v3_ext, ex_constraints[j]);
      v3_ext.end_sequence();
      do_ext(tbs_cert, v3_ext, "X509v3.ExtendedKeyUsage",
             "extended_key_usage");
      }

   tbs_cert.end_sequence();
   tbs_cert.end_explicit(ASN1_Tag(3));
   tbs_cert.end_sequence();

   MemoryVector<byte> tbs_bits = tbs_cert.get_contents();
   MemoryVector<byte> sig = signer->sign_message(tbs_bits);

   DER_Encoder full_cert;
   full_cert.start_sequence();
   full_cert.add_raw_octets(tbs_bits);
   DER::encode(full_cert, sig_algo);
   DER::encode(full_cert, sig, BIT_STRING);
   full_cert.end_sequence();

   DataSource_Memory source(full_cert.get_contents());

   return X509_Certificate(source);
   }

/*************************************************
* Handle encoding a v3 extension                 *
*************************************************/
void X509_CA::do_ext(DER_Encoder& new_cert, DER_Encoder& extension,
                     const std::string& oid, const std::string& opt)
   {
   std::string EXT_SETTING = "yes";

   if(opt != "")
      {
      EXT_SETTING = Config::get_string("x509/exts/" + opt);

      if(EXT_SETTING == "")
         throw Exception("X509_CA: No policy setting for using " + oid);
      }

   if(EXT_SETTING == "no")
      return;
   else if(EXT_SETTING == "yes" || EXT_SETTING == "noncritical" ||
           EXT_SETTING == "critical")
      {
      Extension extn(oid, extension.get_contents());
      if(EXT_SETTING == "critical")
         extn.critical = true;
      DER::encode(new_cert, extn);
      }
   else
      throw Invalid_Argument("X509_CA:: Invalid value for option x509/exts/" +
                             opt + " of " + EXT_SETTING);
   }

/*************************************************
* Create a new, empty CRL                        *
*************************************************/
X509_CRL X509_CA::new_crl(u32bit next_update) const
   {
   std::vector<CRL_Entry> empty;
   return make_crl(empty, 1, next_update);
   }

/*************************************************
* Update a CRL with new entries                  *
*************************************************/
X509_CRL X509_CA::update_crl(const X509_CRL& crl,
                             const std::vector<CRL_Entry>& new_revoked,
                             u32bit next_update) const
   {
   std::vector<CRL_Entry> already_revoked = crl.get_revoked();
   std::vector<CRL_Entry> all_revoked;

   X509_Store store;
   store.add_cert(cert, true);
   if(store.add_crl(crl) != VERIFIED)
      throw Invalid_Argument("X509_CA::update_crl: Invalid CRL provided");

   std::set<SecureVector<byte> > removed_from_crl;
   for(u32bit j = 0; j != new_revoked.size(); j++)
      {
      if(new_revoked[j].reason == DELETE_CRL_ENTRY)
         removed_from_crl.insert(new_revoked[j].serial);
      else
         all_revoked.push_back(new_revoked[j]);
      }

   for(u32bit j = 0; j != already_revoked.size(); j++)
      {
      std::set<SecureVector<byte> >::const_iterator i;
      i = removed_from_crl.find(already_revoked[j].serial);

      if(i == removed_from_crl.end())
         all_revoked.push_back(already_revoked[j]);
      }
   std::sort(all_revoked.begin(), all_revoked.end());

   std::vector<CRL_Entry> cert_list;
   std::unique_copy(all_revoked.begin(), all_revoked.end(),
                    std::back_inserter(cert_list));

   return make_crl(cert_list, crl.crl_number() + 1, next_update);
   }

/*************************************************
* Create a CRL                                   *
*************************************************/
X509_CRL X509_CA::make_crl(const std::vector<CRL_Entry>& revoked,
                           u32bit crl_number, u32bit next_update) const
   {
   const u32bit X509_CRL_VERSION = 1;

   if(next_update == 0)
      next_update = Config::get_time("x509/crl/next_update");

   DER_Encoder tbs_crl;

   const u64bit current_time = system_time();

   tbs_crl.start_sequence();
   DER::encode(tbs_crl, X509_CRL_VERSION);
   DER::encode(tbs_crl, ca_sig_algo);
   DER::encode(tbs_crl, cert.subject_dn());
   DER::encode(tbs_crl, X509_Time(current_time));
   DER::encode(tbs_crl, X509_Time(current_time + next_update));

   if(revoked.size())
      {
      tbs_crl.start_sequence();
      for(u32bit j = 0; j != revoked.size(); j++)
         DER::encode(tbs_crl, revoked[j]);
      tbs_crl.end_sequence();
      }

   tbs_crl.start_explicit(ASN1_Tag(0));
   tbs_crl.start_sequence();

   DER_Encoder crl_ext;

   if(cert.subject_key_id().size())
      {
      crl_ext.start_sequence();
      crl_ext.start_explicit(ASN1_Tag(0));
      DER::encode(crl_ext, cert.subject_key_id(), OCTET_STRING);
      crl_ext.end_explicit(ASN1_Tag(0));
      crl_ext.end_sequence();
      do_ext(tbs_crl, crl_ext, "X509v3.AuthorityKeyIdentifier",
             "authority_key_id");
      }

   if(crl_number)
      {
      DER::encode(crl_ext, crl_number);
      do_ext(tbs_crl, crl_ext, "X509v3.CRLNumber", "crl_number");
      }

   tbs_crl.end_sequence();
   tbs_crl.end_explicit(ASN1_Tag(0));
   tbs_crl.end_sequence();

   MemoryVector<byte> tbs_bits = tbs_crl.get_contents();
   MemoryVector<byte> sig = signer->sign_message(tbs_bits);

   DER_Encoder full_crl;
   full_crl.start_sequence();
   full_crl.add_raw_octets(tbs_bits);
   DER::encode(full_crl, ca_sig_algo);
   DER::encode(full_crl, sig, BIT_STRING);
   full_crl.end_sequence();

   DataSource_Memory source(full_crl.get_contents());

   return X509_CRL(source);
   }

/*************************************************
* Return the CA's certificate                    *
*************************************************/
X509_Certificate X509_CA::ca_certificate() const
   {
   return cert;
   }

/*************************************************
* X509_CA Destructor                             *
*************************************************/
X509_CA::~X509_CA()
   {
   delete signer;
   }

}
