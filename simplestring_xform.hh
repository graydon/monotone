#ifndef __SIMPLESTRING_XFORM_HH__
#define __SIMPLESTRING_XFORM_HH__

#include "vector.hh"

std::string uppercase(std::string const & in);
std::string lowercase(std::string const & in);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out,
                      bool diff_compat);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out,
                      bool diff_compat);

void join_lines(std::vector<std::string> const & in,
                std::string & out,
                std::string const & linesep);

void join_lines(std::vector<std::string> const & in,
                std::string & out);

template< class T >
std::vector< T > split_into_words(T const & in)
{
  std::string const & instr = in();
  std::vector< T > out;

  std::string::size_type begin = 0;
  std::string::size_type end = instr.find_first_of(" ", begin);

  while (end != std::string::npos && end >= begin)
    {
      out.push_back(T(instr.substr(begin, end-begin)));
      begin = end + 1;
      if (begin >= instr.size())
        break;
      end = instr.find_first_of(" ", begin);
    }
  if (begin < instr.size())
    out.push_back(T(instr.substr(begin, instr.size() - begin)));

  return out;
}

template< class Container >
typename Container::value_type join_words(Container const & in, std::string const & sep = " ")
{
  std::string str;
  typename Container::const_iterator iter = in.begin();
  while (iter != in.end())
    {
      str += (*iter)();
      iter++;
      if (iter != in.end())
        str += sep;
    }
  typedef typename Container::value_type result_type;
  return result_type(str);
}

void prefix_lines_with(std::string const & prefix,
                       std::string const & lines,
                       std::string & out);

// append after removing all whitespace
void append_without_ws(std::string & appendto, std::string const & s);

// remove all whitespace
std::string remove_ws(std::string const & s);

// remove leading and trailing whitespace
std::string trim_ws(std::string const & s);

// Local Variables:
// mode: C++
// fill-column: 76
// c-file-style: "gnu"
// indent-tabs-mode: nil
// End:
// vim: et:sw=2:sts=2:ts=2:cino=>2s,{s,\:s,+s,t0,g0,^-2,e-2,n-2,p2s,(0,=s:

#endif
