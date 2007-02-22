#include <boost/shared_ptr.hpp>
#include "../numeric_vocab.hh"
#include "../netxx/stream.h"

class ssh_agent_platform {
private:
  boost::shared_ptr<Netxx::Stream> stream;

public:
  bool connect();
  bool disconnect();
  bool connected();
  void write_data(std::string const & data);
  void read_data(u32 const len, std::string & out);
};
