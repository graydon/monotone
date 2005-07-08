#include <string.h>

#include "cryptopp/hmac.h"
#include "cryptopp/sha.h"

#include "sanity.hh"
#include "hmac.hh"
#include "vocab.hh"
#include "constants.hh"

chained_hmac::chained_hmac(netsync_session_key const & session_key) :
  key(session_key)
{
  I(hmac_length == CryptoPP::SHA::DIGESTSIZE);
  memset(chain_val, 0, sizeof(chain_val));
}

void
chained_hmac::set_key(netsync_session_key const & session_key)
{
  key = session_key;
}

std::string
chained_hmac::process(std::string const & str, size_t pos, size_t n)
{
  I(pos < str.size());
  if (n == std::string::npos)
    n = str.size() - pos;

  I(pos + n <= str.size());

  CryptoPP::HMAC<CryptoPP::SHA> 
      hmac(reinterpret_cast<const byte *>(key().data()), 
        constants::netsync_session_key_length_in_bytes);
  hmac.Update(reinterpret_cast<const byte *>(chain_val), 
              sizeof(chain_val));
  hmac.Update(reinterpret_cast<const byte *>(str.data() + pos),
              n);
  hmac.Final(reinterpret_cast<byte *>(chain_val));
  
  std::string out(chain_val, sizeof(chain_val));
  return out;
}
