#ifndef __URL_HH__
#define __URL_HH__

#include <string>

bool parse_utf8_url(std::string const & utf8,
		    std::string & proto,
		    std::string & user,
		    std::string & host,	       
		    std::string & path,
		    std::string & group,
		    unsigned long & port);

bool parse_url(url const & u,
	       std::string & proto,
	       std::string & user,
	       std::string & host,	       
	       std::string & path,
	       std::string & group,
	       unsigned long & port);

#endif // __URL_HH__
