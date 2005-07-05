#ifndef __HMAC_HH__
#define __HMAC_HH__

#include <string>

#include "cryptopp/hmac.h"
#include "cryptopp/sha.h"

#include "vocab.hh"

struct chained_hmac
{
  public:
    chained_hmac(netsync_session_key const & session_key);
    void set_key(netsync_session_key const & session_key);
    std::string process(std::string const & str, size_t pos = 0, 
        size_t n = std::string::npos);

    static size_t const hmac_length = CryptoPP::SHA::DIGESTSIZE;

  private:
    netsync_session_key key;
    char chain_val[hmac_length];
};




#endif // __HMAC_HH__

