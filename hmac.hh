#ifndef __HMAC_HH__
#define __HMAC_HH__

#include <string>

#include "botan/botan.h"
#include "vocab.hh"
#include "constants.hh"

struct chained_hmac
{
  public:
    chained_hmac(netsync_session_key const & session_key);
    void set_key(netsync_session_key const & session_key);
    std::string process(std::string const & str, size_t pos = 0, 
        size_t n = std::string::npos);

    size_t const hmac_length;

  private:
    Botan::SymmetricKey key;
    std::string chain_val;
};




#endif // __HMAC_HH__

