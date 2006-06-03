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

// remove all whitespace
std::string remove_ws(std::string const & s);

// remove leading and trailing whitespace
std::string trim_ws(std::string const & s);

// line-ending conversion
void line_end_convert(std::string const & linesep, std::string const & src, std::string & dst);

#endif
