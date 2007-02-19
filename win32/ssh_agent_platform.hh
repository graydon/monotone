#include "../numeric_vocab.hh"

class ssh_agent_platform {
public:
  bool connect();
  bool disconnect();
  bool connected();
  void write_data(std::string const & data);
  void read_data(u32 const len, std::string & out);
};
