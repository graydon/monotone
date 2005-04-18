/*************************************************
* X.509 Certificates Header File                 *
* (C) 1999-2004 The Botan Project                *
*************************************************/

#ifndef BOTAN_X509_CERTS_H__
#define BOTAN_X509_CERTS_H__

#include <botan/x509_obj.h>
#include <botan/x509_key.h>
#include <botan/pkcs8.h>
#include <map>

namespace Botan {

static const u32bit NO_CERT_PATH_LIMIT = 0xFFFFFFFF;

/*************************************************
* X.509 Certificate                              *
*************************************************/
class X509_Certificate : public X509_Object
   {
   public:
      u32bit x509_version() const;

      std::string start_time() const;
      std::string end_time() const;

      std::string subject_info(const std::string&) const;
      std::string issuer_info(const std::string&) const;
      X509_DN issuer_dn() const;
      X509_DN subject_dn() const;

      MemoryVector<byte> serial_number() const;
      BigInt serial_number_bn() const;
      X509_PublicKey* subject_public_key() const;
      bool self_signed() const;
      bool has_SKID() const;

      bool is_CA_cert() const;
      u32bit path_limit() const;
      Key_Constraints constraints() const;
      std::vector<OID> ex_constraints() const;
      std::vector<OID> policies() const;

      MemoryVector<byte> authority_key_id() const;
      MemoryVector<byte> subject_key_id() const;

      bool operator==(const X509_Certificate&) const;

      void force_decode();

      X509_Certificate(DataSource&);
      X509_Certificate(const std::string&);
   private:
      friend class X509_CA;
      X509_Certificate() {}
      void handle_v3_extension(const Extension&);

      std::multimap<std::string, std::string> subject, issuer;
      MemoryVector<byte> v3_issuer_key_id, v3_subject_key_id;
      MemoryVector<byte> v2_issuer_key_id, v2_subject_key_id;
      MemoryVector<byte> pub_key;
      std::vector<OID> ex_constraints_list, policies_list;
      BigInt serial;
      X509_Time start, end;
      Key_Constraints constraints_value;
      u32bit version, max_path_len;
      bool is_ca;
   };

/*************************************************
* X.509 Certificate Comparison                   *
*************************************************/
bool operator!=(const X509_Certificate&, const X509_Certificate&);

}

#endif
