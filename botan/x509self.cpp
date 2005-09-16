/*************************************************
* X.509 Certificate Authority Source File        *
* (C) 1999-2005 The Botan Project                *
*************************************************/

#include <botan/x509self.h>
#include <botan/x509_ca.h>
#include <botan/conf.h>
#include <botan/look_pk.h>
#include <botan/oids.h>
#include <botan/pipe.h>
#include <memory>

namespace Botan {

namespace {

/*************************************************
* Shared setup for self-signed items             *
*************************************************/
MemoryVector<byte> shared_setup(const X509_Cert_Options& opts,
                                const PKCS8_PrivateKey& key)
   {
   const PKCS8_PrivateKey* key_pointer = &key;
   if(!dynamic_cast<const PK_Signing_Key*>(key_pointer))
      throw Invalid_Argument("Key type " + key.algo_name() + " cannot sign");

   opts.sanity_check();

   Pipe key_encoder;
   key_encoder.start_msg();
   X509::encode(key, key_encoder, RAW_BER);
   key_encoder.end_msg();

   return key_encoder.read_all();
   }

/*************************************************
* Load information from the X509_Cert_Options    *
*************************************************/
void load_info(const X509_Cert_Options& opts, X509_DN& subject_dn,
               AlternativeName& subject_alt)
   {
   subject_dn.add_attribute("X520.CommonName", opts.common_name);
   subject_dn.add_attribute("X520.Country", opts.country);
   subject_dn.add_attribute("X520.State", opts.state);
   subject_dn.add_attribute("X520.Locality", opts.locality);
   subject_dn.add_attribute("X520.Organization", opts.organization);
   subject_dn.add_attribute("X520.OrganizationalUnit", opts.org_unit);
   subject_dn.add_attribute("X520.SerialNumber", opts.serial_number);
   subject_alt = AlternativeName(opts.email, opts.uri, opts.dns);
   subject_alt.add_othername(OIDS::lookup("PKIX.XMPPAddr"),
                             opts.xmpp, UTF8_STRING);
   }

/*************************************************
* Choose a signing format for the key            *
*************************************************/
PK_Signer* choose_sig_format(const PKCS8_PrivateKey& key,
                             AlgorithmIdentifier& sig_algo)
   {
   std::string padding;
   Signature_Format format;
   Config::choose_sig_format(key.algo_name(), padding, format);

   sig_algo.oid = OIDS::lookup(key.algo_name() + "/" + padding);
   sig_algo.parameters = key.DER_encode_params();

   const PK_Signing_Key& sig_key = dynamic_cast<const PK_Signing_Key&>(key);

   return get_pk_signer(sig_key, padding, format);
   }

/*************************************************
* Encode an attribute for PKCS #10 request       *
*************************************************/
void do_attribute(DER_Encoder& tbs_req, DER_Encoder& attr_bits,
                  const std::string& oid_str)
   {
   Attribute attr(OIDS::lookup(oid_str), attr_bits.get_contents());
   DER::encode(tbs_req, attr);
   }

/*************************************************
* Encode an Extension for a PKCS #10 request     *
*************************************************/
void do_ext(DER_Encoder& attr_encoder, DER_Encoder& extn_bits,
            const std::string& oid)
   {
   Extension extn(oid, extn_bits.get_contents());
   DER::encode(attr_encoder, extn);
   }

/*************************************************
* Encode X.509 extensions for a PKCS #10 request *
*************************************************/
void encode_extensions(DER_Encoder& attr_encoder,
                       const AlternativeName& subject_alt,
                       bool is_CA, u32bit path_limit,
                       Key_Constraints constraints,
                       const std::vector<OID>& ex_constraints)
   {
   DER_Encoder v3_ext;

   attr_encoder.start_sequence();
   if(is_CA)
      {
      v3_ext.start_sequence();
      DER::encode(v3_ext, true);
      if(path_limit != NO_CERT_PATH_LIMIT)
         DER::encode(v3_ext, path_limit);
      v3_ext.end_sequence();
      do_ext(attr_encoder, v3_ext, "X509v3.BasicConstraints");
      }

   if(subject_alt.has_items())
      {
      DER::encode(v3_ext, subject_alt);
      do_ext(attr_encoder, v3_ext, "X509v3.SubjectAlternativeName");
      }

   if(constraints != NO_CONSTRAINTS)
      {
      DER::encode(v3_ext, constraints);
      do_ext(attr_encoder, v3_ext, "X509v3.KeyUsage");
      }

   if(ex_constraints.size())
      {
      v3_ext.start_sequence();
      for(u32bit j = 0; j != ex_constraints.size(); j++)
         DER::encode(v3_ext, ex_constraints[j]);
      v3_ext.end_sequence();
      do_ext(attr_encoder, v3_ext, "X509v3.ExtendedKeyUsage");
      }
   attr_encoder.end_sequence();
   }

}

namespace X509 {

/*************************************************
* Create a new self-signed X.509 certificate     *
*************************************************/
X509_Certificate create_self_signed_cert(const X509_Cert_Options& opts,
                                         const PKCS8_PrivateKey& key)
   {
   AlgorithmIdentifier sig_algo;
   X509_DN subject_dn;
   AlternativeName subject_alt;

   MemoryVector<byte> pub_key = shared_setup(opts, key);
   std::auto_ptr<PK_Signer> signer(choose_sig_format(key, sig_algo));
   load_info(opts, subject_dn, subject_alt);

   Key_Constraints constraints;
   if(opts.is_CA)
      constraints = Key_Constraints(KEY_CERT_SIGN | CRL_SIGN);
   else
      constraints = find_constraints(key, opts.constraints);

   return X509_CA::make_cert(signer.get(), sig_algo, pub_key,
                             MemoryVector<byte>(), opts.start, opts.end,
                             subject_dn, subject_dn,
                             opts.is_CA, opts.path_limit,
                             subject_alt, constraints, opts.ex_constraints);
   }

/*************************************************
* Create a PKCS #10 certificate request          *
*************************************************/
PKCS10_Request create_cert_req(const X509_Cert_Options& opts,
                               const PKCS8_PrivateKey& key)
   {
   AlgorithmIdentifier sig_algo;
   X509_DN subject_dn;
   AlternativeName subject_alt;

   MemoryVector<byte> pub_key = shared_setup(opts, key);
   std::auto_ptr<PK_Signer> signer(choose_sig_format(key, sig_algo));
   load_info(opts, subject_dn, subject_alt);

   const u32bit PKCS10_VERSION = 0;

   DER_Encoder tbs_req;

   tbs_req.start_sequence();
   DER::encode(tbs_req, PKCS10_VERSION);
   DER::encode(tbs_req, subject_dn);
   tbs_req.add_raw_octets(pub_key);

   tbs_req.start_explicit(ASN1_Tag(0));

   DER_Encoder attr_encoder;

   if(opts.challenge != "")
      {
      ASN1_String challenge(opts.challenge, DIRECTORY_STRING);
      DER::encode(attr_encoder, challenge);
      do_attribute(tbs_req, attr_encoder, "PKCS9.ChallengePassword");
      }

   Key_Constraints constraints;
   if(opts.is_CA)
      constraints = Key_Constraints(KEY_CERT_SIGN | CRL_SIGN);
   else
      constraints = find_constraints(key, opts.constraints);

   encode_extensions(attr_encoder, subject_alt, opts.is_CA, opts.path_limit,
                     constraints, opts.ex_constraints);
   do_attribute(tbs_req, attr_encoder, "PKCS9.ExtensionRequest");

   tbs_req.end_explicit(ASN1_Tag(0));

   tbs_req.end_sequence();

   MemoryVector<byte> tbs_bits = tbs_req.get_contents();
   MemoryVector<byte> sig = signer->sign_message(tbs_bits);

   DER_Encoder full_req;
   full_req.start_sequence();
   full_req.add_raw_octets(tbs_bits);
   DER::encode(full_req, sig_algo);
   DER::encode(full_req, sig, BIT_STRING);
   full_req.end_sequence();

   DataSource_Memory source(full_req.get_contents());

   return PKCS10_Request(source);
   }

}

}
