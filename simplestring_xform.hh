#ifndef __SIMPLESTRING_XFORM_HH__
#define __SIMPLESTRING_XFORM_HH__

#include <vector>
#include <string>

std::string uppercase(std::string const & in);
std::string lowercase(std::string const & in);

void split_into_lines(std::string const & in,
                      std::vector<std::string> & out);

void split_into_lines(std::string const & in,
                      std::string const & encoding,
                      std::vector<std::string> & out);

void join_lines(std::vector<std::string> const & in,
                std::string & out,
                std::string const & linesep);

void join_lines(std::vector<std::string> const & in,
                std::string & out);

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
