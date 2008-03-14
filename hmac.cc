
#include "base.hh"
#include "botan/botan.h"

#include "sanity.hh"
#include "hmac.hh"
#include "vocab.hh"
#include "constants.hh"

using std::string;

chained_hmac::chained_hmac(netsync_session_key const & session_key, bool active) :
  hmac_length(constants::sha1_digest_length),
  active(active),
  key(reinterpret_cast<Botan::byte const *>(session_key().data()),
      session_key().size()),
  engine(new Botan::MAC_Filter("HMAC(SHA-160)", key,
                               constants::sha1_digest_length))
{
  chain_val.assign(hmac_length, 0x00);
}

void
chained_hmac::set_key(netsync_session_key const & session_key)
{
  if (active)
    {
      key = Botan::SymmetricKey(reinterpret_cast<Botan::byte const *>(session_key().data()),
				session_key().size());
      engine.reset();
      engine.append(new Botan::MAC_Filter("HMAC(SHA-160)", key,
                                          constants::sha1_digest_length));
    }
}

string
chained_hmac::process(string const & str, size_t pos, size_t n)
{
  I(active);

  I(pos < str.size());
  if (n == string::npos)
    n = str.size() - pos;

  I(pos + n <= str.size());

  engine.start_msg();
  engine.write(chain_val);
  engine.write(reinterpret_cast<Botan::byte const *>(str.data() + pos), n);
  engine.end_msg();

  chain_val = engine.read_all_as_string(Botan::Pipe::LAST_MESSAGE);
  I(chain_val.size() == constants::sha1_digest_length);

  return chain_val;
}

string
chained_hmac::process(string_queue const & str, size_t pos, size_t n)
{
  I(active);

  I(pos < str.size());
  if (n == string::npos)
    n = str.size() - pos;

  I(pos + n <= str.size());

  engine.start_msg();
  engine.write(chain_val);
  engine.write(reinterpret_cast<Botan::byte const *>(str.front_pointer(n)
                                                     + pos), n);
  engine.end_msg();

  chain_val = engine.read_all_as_string(Botan::Pipe::LAST_MESSAGE);
  I(chain_val.size() == constants::sha1_digest_length);

  return chain_val;
}

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:
